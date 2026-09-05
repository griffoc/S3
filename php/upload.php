<?php
require 'db.php';

try {
    $rawInput = file_get_contents('php://input');

    if (!empty($rawInput)) {
        $targetDir = $_SERVER['DOCUMENT_ROOT'] . '/fridge_stats/uploads/';
        $uniqueName = uniqid() . '.txt';
        $targetFile = $targetDir . $uniqueName;

        $file_put = file_put_contents($targetFile, $rawInput . PHP_EOL);

        if( $file_put  === false) {
            http_response_code(500);
            echo "Failed to create target file: " . $targetFile;
        }
        else {
            $rowsAffected = $pdo->exec("LOAD DATA LOCAL INFILE '$targetFile' INTO TABLE FridgeStats.usage_by_second
                FIELDS TERMINATED BY ','
                LINES TERMINATED BY '\n'
                IGNORE 1 ROWS
                (power, current, bus_voltage, measurement_date)");

            if ($rowsAffected === false) {
                // Handle error here
                http_response_code(500);
                $error = $pdo->errorInfo();
                echo "Query failed: " . $error[0];
            }
            else {
                // unlink($targetFile);

                http_response_code(200);
                echo "Data received successfully: " . $rowsAffected . " rows inserted";
            }
        }
    }
    else {
        http_response_code(400);
        echo "No data received";
    }
}
catch(Exception $e) {
    http_response_code(500);
    echo $e->getMessage();
}
?>
