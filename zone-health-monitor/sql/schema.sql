-- rr_zone table: Maps resource records to geographic Kubernetes zones.
-- This table is used by the zone health monitor to determine which rr records
-- belong to which Kubernetes topology zone for automated failover.
--
-- The 'zone' column stores the Kubernetes topology.kubernetes.io/zone label value
-- (e.g., 'ro2', 'ro3', 'de4'). No integer ID mapping is needed.
--
-- The 'active' column controls whether the health monitor manages this mapping.
-- Set active='N' to exclude a record from automated failover without deleting the mapping.

CREATE TABLE IF NOT EXISTS rr_zone (
  id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  rr_id INT UNSIGNED NOT NULL UNIQUE,           -- FK to rr.id
  zone VARCHAR(63) NOT NULL,                     -- Kubernetes topology zone name
  active ENUM('Y','N') NOT NULL DEFAULT 'Y',    -- Whether the monitor manages this mapping
  INDEX idx_rr_zone_zone (zone),
  CONSTRAINT fk_rr_zone_rr FOREIGN KEY (rr_id) REFERENCES rr(id) ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
