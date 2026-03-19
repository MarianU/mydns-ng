// Package mydns provides database client functionality for the zone health monitor.
package mydns

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/go-sql-driver/mysql"
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

// DBClient provides database operations for MyDNS-NG zone health monitoring.
type DBClient struct {
	db *sql.DB
}

// NewDBClient creates a new database client with connection pooling.
func NewDBClient(cfg *DBConfig) (*DBClient, error) {
	db, err := sql.Open("mysql", cfg.DSN())
	if err != nil {
		return nil, fmt.Errorf("failed to open database: %w", err)
	}

	// Configure connection pool.
	db.SetMaxOpenConns(10)
	db.SetMaxIdleConns(5)
	db.SetConnMaxLifetime(5 * time.Minute)
	db.SetConnMaxIdleTime(1 * time.Minute)

	// Verify connection.
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := db.PingContext(ctx); err != nil {
		_ = db.Close()
		return nil, fmt.Errorf("failed to ping database: %w", err)
	}

	klog.V(2).Info("Successfully connected to MyDNS database")
	return &DBClient{db: db}, nil
}

// DeactivateZoneRecords deactivates all managed records for a zone using SELECT...FOR UPDATE NOWAIT.
// Returns the number of records deactivated. If another monitor holds the lock, returns 0 with no error.
func (c *DBClient) DeactivateZoneRecords(ctx context.Context, zone string) (int64, error) {
	var affected int64

	err := c.withRetry(ctx, func() error {
		tx, err := c.db.BeginTx(ctx, nil)
		if err != nil {
			return fmt.Errorf("failed to begin transaction: %w", err)
		}
		defer tx.Rollback() //nolint:errcheck // rollback after commit is harmless

		// Lock the relevant records with NOWAIT to avoid blocking.
		lockQuery := `SELECT rr.id, rr.zone FROM rr
			INNER JOIN rr_zone ON rr.id = rr_zone.rr_id
			WHERE rr_zone.zone = ? AND rr_zone.active = 'Y' AND rr.active = 'Y'
			FOR UPDATE NOWAIT`

		rows, err := tx.QueryContext(ctx, lockQuery, zone)
		if err != nil {
			if isLockNowaitError(err) {
				klog.V(2).Infof("Lock contention for zone %q, another monitor is handling failover", zone)
				affected = 0
				return nil
			}
			return fmt.Errorf("failed to lock zone records: %w", err)
		}

		recordIDs, soaZoneIDs, err := scanRecordRows(rows)
		if err != nil {
			return err
		}

		if len(recordIDs) == 0 {
			klog.V(2).Infof("No active records to deactivate for zone %q", zone)
			affected = 0
			return nil
		}

		// Deactivate the records.
		result, err := execInPlaceholder(ctx, tx, "UPDATE rr SET active = 'N' WHERE id IN (%s)", recordIDs)
		if err != nil {
			return fmt.Errorf("failed to deactivate zone records: %w", err)
		}

		affected, _ = result.RowsAffected()

		// Increment SOA serial for each affected DNS zone.
		if err := c.incrementSerials(ctx, tx, soaZoneIDs); err != nil {
			return err
		}

		if err := tx.Commit(); err != nil {
			return fmt.Errorf("failed to commit transaction: %w", err)
		}

		return nil
	})

	if err != nil {
		return 0, err
	}

	if affected > 0 {
		klog.V(2).Infof("Deactivated %d records for zone %q", affected, zone)
	}
	return affected, nil
}

// ReactivateZoneRecords reactivates all managed records for a zone using SELECT...FOR UPDATE NOWAIT.
// Returns the number of records reactivated. If another monitor holds the lock, returns 0 with no error.
func (c *DBClient) ReactivateZoneRecords(ctx context.Context, zone string) (int64, error) {
	var affected int64

	err := c.withRetry(ctx, func() error {
		tx, err := c.db.BeginTx(ctx, nil)
		if err != nil {
			return fmt.Errorf("failed to begin transaction: %w", err)
		}
		defer tx.Rollback() //nolint:errcheck // rollback after commit is harmless

		// Lock the relevant records with NOWAIT to avoid blocking.
		lockQuery := `SELECT rr.id, rr.zone FROM rr
			INNER JOIN rr_zone ON rr.id = rr_zone.rr_id
			WHERE rr_zone.zone = ? AND rr_zone.active = 'Y' AND rr.active = 'N'
			FOR UPDATE NOWAIT`

		rows, err := tx.QueryContext(ctx, lockQuery, zone)
		if err != nil {
			if isLockNowaitError(err) {
				klog.V(2).Infof("Lock contention for zone %q, another monitor is handling failover", zone)
				affected = 0
				return nil
			}
			return fmt.Errorf("failed to lock zone records: %w", err)
		}

		recordIDs, soaZoneIDs, err := scanRecordRows(rows)
		if err != nil {
			return err
		}

		if len(recordIDs) == 0 {
			klog.V(2).Infof("No inactive records to reactivate for zone %q", zone)
			affected = 0
			return nil
		}

		// Reactivate the records.
		result, err := execInPlaceholder(ctx, tx, "UPDATE rr SET active = 'Y' WHERE id IN (%s)", recordIDs)
		if err != nil {
			return fmt.Errorf("failed to reactivate zone records: %w", err)
		}

		affected, _ = result.RowsAffected()

		// Increment SOA serial for each affected DNS zone.
		if err := c.incrementSerials(ctx, tx, soaZoneIDs); err != nil {
			return err
		}

		if err := tx.Commit(); err != nil {
			return fmt.Errorf("failed to commit transaction: %w", err)
		}

		return nil
	})

	if err != nil {
		return 0, err
	}

	if affected > 0 {
		klog.V(2).Infof("Reactivated %d records for zone %q", affected, zone)
	}
	return affected, nil
}

// GetManagedZones returns the list of distinct zone names from rr_zone where active='Y'.
func (c *DBClient) GetManagedZones(ctx context.Context) ([]string, error) {
	var zones []string

	err := c.withRetry(ctx, func() error {
		rows, err := c.db.QueryContext(ctx, "SELECT DISTINCT zone FROM rr_zone WHERE active = 'Y'")
		if err != nil {
			return fmt.Errorf("failed to query managed zones: %w", err)
		}

		zones, err = scanZoneRows(rows)
		return err
	})

	if err != nil {
		return nil, err
	}

	klog.V(4).Infof("Found %d managed zones", len(zones))
	return zones, nil
}

// scanRecordRows scans rows from a record query, closes the rows, and returns IDs and SOA zone IDs.
func scanRecordRows(rows *sql.Rows) ([]int64, map[int64]struct{}, error) {
	defer func() {
		_ = rows.Close()
	}()

	var recordIDs []int64
	soaZoneIDs := make(map[int64]struct{})

	for rows.Next() {
		var rrID, soaZoneID int64
		if err := rows.Scan(&rrID, &soaZoneID); err != nil {
			return nil, nil, fmt.Errorf("failed to scan record row: %w", err)
		}
		recordIDs = append(recordIDs, rrID)
		soaZoneIDs[soaZoneID] = struct{}{}
	}
	if err := rows.Err(); err != nil {
		return nil, nil, fmt.Errorf("failed to iterate record rows: %w", err)
	}

	return recordIDs, soaZoneIDs, nil
}

// scanZoneRows scans rows from a zone query, closes the rows, and returns zone names.
func scanZoneRows(rows *sql.Rows) ([]string, error) {
	defer func() {
		_ = rows.Close()
	}()

	var zones []string
	for rows.Next() {
		var zone string
		if err := rows.Scan(&zone); err != nil {
			return nil, fmt.Errorf("failed to scan zone row: %w", err)
		}
		zones = append(zones, zone)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("failed to iterate zone rows: %w", err)
	}

	return zones, nil
}

// incrementSerials increments the SOA serial for multiple DNS zones within a transaction.
func (c *DBClient) incrementSerials(ctx context.Context, tx *sql.Tx, soaZoneIDs map[int64]struct{}) error {
	for zoneID := range soaZoneIDs {
		result, err := tx.ExecContext(ctx, "UPDATE soa SET serial = serial + 1 WHERE id = ?", zoneID)
		if err != nil {
			return fmt.Errorf("failed to increment SOA serial: %w", err)
		}

		rowsAffected, _ := result.RowsAffected()
		if rowsAffected == 0 {
			return fmt.Errorf("no SOA record found for zone ID %d", zoneID)
		}

		klog.V(4).Infof("Incremented SOA serial for zone ID %d", zoneID)
	}
	return nil
}

// isLockNowaitError returns true if the error is a MySQL ER_LOCK_NOWAIT (3572).
func isLockNowaitError(err error) bool {
	var mysqlErr *mysql.MySQLError
	return errors.As(err, &mysqlErr) && mysqlErr.Number == 3572
}

// execInPlaceholder executes a query with an IN (%s) placeholder expanded for the given IDs.
func execInPlaceholder(ctx context.Context, tx *sql.Tx, queryFmt string, ids []int64) (sql.Result, error) {
	if len(ids) == 0 {
		return nil, fmt.Errorf("no IDs provided for IN clause")
	}

	placeholders := make([]byte, 0, len(ids)*2-1)
	args := make([]interface{}, len(ids))
	for i, id := range ids {
		if i > 0 {
			placeholders = append(placeholders, ',')
		}
		placeholders = append(placeholders, '?')
		args[i] = id
	}

	query := fmt.Sprintf(queryFmt, placeholders)
	return tx.ExecContext(ctx, query, args...)
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

		// Don't retry on non-transient errors.
		if lastErr == sql.ErrNoRows {
			return lastErr
		}
	}

	return lastErr
}

// Ping checks if the database connection is alive.
func (c *DBClient) Ping() bool {
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	return c.db.PingContext(ctx) == nil
}

// Close closes the database connection pool.
func (c *DBClient) Close() error {
	if c.db != nil {
		klog.V(2).Info("Closing database connection")
		return c.db.Close()
	}
	return nil
}
