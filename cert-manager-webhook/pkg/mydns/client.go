// Package mydns provides database client functionality for MyDNS-NG.
package mydns

import (
	"context"
	"database/sql"
	"fmt"
	"time"

	_ "github.com/go-sql-driver/mysql"
	"k8s.io/klog/v2"
)

// DBConfig holds database connection configuration.
type DBConfig struct {
	Host     string
	Port     string
	User     string
	Password string
	Database string
}

// DSN returns the MySQL data source name.
func (c *DBConfig) DSN() string {
	return fmt.Sprintf("%s:%s@tcp(%s:%s)/%s?parseTime=true&timeout=10s",
		c.User, c.Password, c.Host, c.Port, c.Database)
}

// Client provides database operations for MyDNS-NG.
type Client interface {
	// GetZone returns the zone ID for the given origin (e.g., "example.com.").
	GetZone(ctx context.Context, origin string) (int64, error)

	// CreateTXTRecord creates a TXT record in the rr table.
	CreateTXTRecord(ctx context.Context, zoneID int64, name, data string, ttl int) error

	// DeleteTXTRecord deletes a specific TXT record from the rr table.
	DeleteTXTRecord(ctx context.Context, zoneID int64, name, data string) error

	// Close closes the database connection.
	Close() error
}

// DBClient implements Client using a MySQL/MariaDB connection.
type DBClient struct {
	db *sql.DB
}

// NewDBClient creates a new database client with connection pooling.
func NewDBClient(cfg *DBConfig) (*DBClient, error) {
	db, err := sql.Open("mysql", cfg.DSN())
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	// Configure connection pool
	db.SetMaxOpenConns(10)
	db.SetMaxIdleConns(5)
	db.SetConnMaxLifetime(5 * time.Minute)
	db.SetConnMaxIdleTime(1 * time.Minute)

	// Verify connection
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		db.Close()
		return nil, fmt.Errorf("failed to ping database: %w", err)
	}

	klog.V(2).Info("Successfully connected to MyDNS database")
	return &DBClient{db: db}, nil
}

// GetZone returns the zone ID for the given origin.
// The origin should include the trailing dot (e.g., "example.com.").
func (c *DBClient) GetZone(ctx context.Context, origin string) (int64, error) {
	var zoneID int64

	query := "SELECT id FROM soa WHERE origin = ? AND active = 'Y'"

	err := c.withRetry(ctx, func() error {
		return c.db.QueryRowContext(ctx, query, origin).Scan(&zoneID)
	})

	if err == sql.ErrNoRows {
		return 0, fmt.Errorf("zone not found for origin %q", origin)
	}
	if err != nil {
		return 0, fmt.Errorf("failed to query zone: %w", err)
	}

	klog.V(4).Infof("Found zone ID %d for origin %q", zoneID, origin)
	return zoneID, nil
}

// CreateTXTRecord creates a TXT record in the rr table and increments SOA serial.
// This operation is idempotent - if the record already exists, it succeeds silently.
func (c *DBClient) CreateTXTRecord(ctx context.Context, zoneID int64, name, data string, ttl int) error {
	return c.withRetry(ctx, func() error {
		tx, err := c.db.BeginTx(ctx, nil)
		if err != nil {
			return fmt.Errorf("failed to begin transaction: %w", err)
		}
		defer tx.Rollback() //nolint:errcheck // rollback after commit is harmless

		// Check if record already exists (for idempotency).
		var exists bool
		checkQuery := "SELECT EXISTS(SELECT 1 FROM rr WHERE zone = ? AND name = ? AND type = 'TXT' AND data = ?)"
		if scanErr := tx.QueryRowContext(ctx, checkQuery, zoneID, name, data).Scan(&exists); scanErr != nil {
			return fmt.Errorf("failed to check existing record: %w", scanErr)
		}

		if exists {
			klog.V(2).Infof("TXT record already exists for zone=%d name=%q, skipping creation", zoneID, name)
			return nil
		}

		// Insert the TXT record
		insertQuery := "INSERT INTO rr (zone, name, type, data, ttl, active) VALUES (?, ?, 'TXT', ?, ?, 'Y')"
		result, err := tx.ExecContext(ctx, insertQuery, zoneID, name, data, ttl)
		if err != nil {
			return fmt.Errorf("failed to insert TXT record: %w", err)
		}

		rowsAffected, rowErr := result.RowsAffected()
		if rowErr != nil {
			return fmt.Errorf("failed to get rows affected: %w", rowErr)
		}
		klog.V(2).Infof("Inserted TXT record: zone=%d name=%q rows=%d", zoneID, name, rowsAffected)

		// Increment SOA serial
		if err := c.incrementSerial(ctx, tx, zoneID); err != nil {
			return err
		}

		if err := tx.Commit(); err != nil {
			return fmt.Errorf("failed to commit transaction: %w", err)
		}

		return nil
	})
}

// DeleteTXTRecord deletes a specific TXT record from the rr table.
// This operation is idempotent - if the record doesn't exist, it succeeds silently.
func (c *DBClient) DeleteTXTRecord(ctx context.Context, zoneID int64, name, data string) error {
	return c.withRetry(ctx, func() error {
		tx, err := c.db.BeginTx(ctx, nil)
		if err != nil {
			return fmt.Errorf("failed to begin transaction: %w", err)
		}
		defer tx.Rollback() //nolint:errcheck // rollback after commit is harmless

		// Delete the TXT record.
		deleteQuery := "DELETE FROM rr WHERE zone = ? AND name = ? AND type = 'TXT' AND data = ?"
		result, err := tx.ExecContext(ctx, deleteQuery, zoneID, name, data)
		if err != nil {
			return fmt.Errorf("failed to delete TXT record: %w", err)
		}

		rowsAffected, rowErr := result.RowsAffected()
		if rowErr != nil {
			return fmt.Errorf("failed to get rows affected: %w", rowErr)
		}
		klog.V(2).Infof("Deleted TXT record: zone=%d name=%q rows=%d", zoneID, name, rowsAffected)

		// Only increment serial if we actually deleted something
		if rowsAffected > 0 {
			if err := c.incrementSerial(ctx, tx, zoneID); err != nil {
				return err
			}
		}

		if err := tx.Commit(); err != nil {
			return fmt.Errorf("failed to commit transaction: %w", err)
		}

		return nil
	})
}

// incrementSerial increments the SOA serial for a zone within a transaction.
func (c *DBClient) incrementSerial(ctx context.Context, tx *sql.Tx, zoneID int64) error {
	updateQuery := "UPDATE soa SET serial = serial + 1 WHERE id = ?"
	result, err := tx.ExecContext(ctx, updateQuery, zoneID)
	if err != nil {
		return fmt.Errorf("failed to increment SOA serial: %w", err)
	}

	rowsAffected, rowErr := result.RowsAffected()
	if rowErr != nil {
		return fmt.Errorf("failed to get SOA rows affected: %w", rowErr)
	}
	if rowsAffected == 0 {
		return fmt.Errorf("no SOA record found for zone ID %d", zoneID)
	}

	klog.V(4).Infof("Incremented SOA serial for zone ID %d", zoneID)
	return nil
}

// withRetry executes an operation with exponential backoff retry.
func (c *DBClient) withRetry(ctx context.Context, op func() error) error {
	var lastErr error
	backoff := 100 * time.Millisecond

	for attempt := 0; attempt < 3; attempt++ {
		if attempt > 0 {
			klog.V(3).Infof("Retrying database operation (attempt %d/3)", attempt+1)
			select {
			case <-ctx.Done():
				return ctx.Err()
			case <-time.After(backoff):
				backoff *= 2
			}
		}

		lastErr = op()
		if lastErr == nil {
			return nil
		}

		// Don't retry on non-transient errors
		if lastErr == sql.ErrNoRows {
			return lastErr
		}
	}

	return lastErr
}

// Close closes the database connection pool.
func (c *DBClient) Close() error {
	if c.db != nil {
		klog.V(2).Info("Closing database connection")
		return c.db.Close()
	}
	return nil
}
