
-- MariaDB [(none)]> SET GLOBAL general_log = 'ON';
-- MariaDB [(none)]> SELECT event_time, user_host, thread_id, command_type, argument  FROM mysql.general_log  ORDER BY `general_log`.`event_time`DESC LIMIT 10;

-- SET GLOBAL event_scheduler = ON;

DELIMITER //
CREATE OR REPLACE EVENT move_records_daily
ON SCHEDULE EVERY 1 DAY
STARTS CURRENT_TIMESTAMP
ON COMPLETION PRESERVE
DO CALL run_move_records_daily();
DELIMITER ;


DELIMITER //
CREATE OR REPLACE PROCEDURE run_move_records_daily()
BEGIN

  -- Copy records older than 1 hour to the destination table
  DECLARE cut_off_time DATETIME;
  SET cut_off_time = DATE_FORMAT(NOW() - INTERVAL 1 DAY, '%Y-%m-%d %H:00:00');

  -- Start a transaction for safety
  START TRANSACTION;
        
	INSERT INTO usage_by_hour (power, current, bus_voltage, measurement_date)
    SELECT
      CAST(AVG(CASE WHEN current <= 0 THEN 0 ELSE power END) AS DECIMAL(10,2)),
      CAST(AVG(CASE WHEN current <= 0 THEN 0 ELSE current END) AS DECIMAL(10,2)),
      CAST(AVG(CASE WHEN current <= 0 THEN NULL ELSE bus_voltage END) AS DECIMAL(10,2)),
      DATE_FORMAT(measurement_date, '%Y-%m-%d %H:00:00')
    FROM usage_by_second
    WHERE measurement_date < cut_off_time
    GROUP BY DATE_FORMAT(measurement_date, '%Y-%m-%d %H:00:00');
	
    -- Delete the moved records from the source table
  	DELETE FROM usage_by_second 
  	WHERE measurement_date < cut_off_time;

  -- Commit the transaction
  COMMIT;
END;
//

DELIMITER ;
