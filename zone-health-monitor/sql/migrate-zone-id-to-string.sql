-- Migration: rr_zone zone_id INT → zone VARCHAR(63)
-- 
-- This migration converts the rr_zone table from using integer zone IDs
-- to using Kubernetes topology zone name strings directly.
-- It also adds an 'active' column for operator control over failover management.
--
-- Prerequisites:
--   - Backup the rr_zone table before running
--   - Know the mapping of zone_id integers to zone name strings
--
-- Run against production database before deploying the health monitor.
-- This migration is idempotent for the data updates but not for DDL changes.

-- Step 1: Add the new columns
ALTER TABLE rr_zone ADD COLUMN zone VARCHAR(63) NOT NULL DEFAULT '' AFTER rr_id;
ALTER TABLE rr_zone ADD COLUMN active ENUM('Y','N') NOT NULL DEFAULT 'Y' AFTER zone;

-- Step 2: Migrate existing data
-- Update these mappings to match your environment
UPDATE rr_zone SET zone = 'ro2' WHERE zone_id = 2;
UPDATE rr_zone SET zone = 'ro3' WHERE zone_id = 3;
UPDATE rr_zone SET zone = 'de4' WHERE zone_id = 4;

-- Step 3: Verify migration (should return 0 rows if all zone_ids were mapped)
SELECT * FROM rr_zone WHERE zone = '';

-- Step 4: Drop the old column and add index
ALTER TABLE rr_zone DROP COLUMN zone_id;
ALTER TABLE rr_zone ADD INDEX idx_rr_zone_zone (zone);

-- Step 5: Add foreign key constraint if not present
-- Note: This may fail if there are orphaned rr_zone.rr_id values not in rr.id
ALTER TABLE rr_zone ADD CONSTRAINT fk_rr_zone_rr
  FOREIGN KEY (rr_id) REFERENCES rr(id) ON DELETE CASCADE;
