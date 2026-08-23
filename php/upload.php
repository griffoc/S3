<?php
require 'db.php';

if (isset($_FILES['file']) && $_FILES['file']['error'] === UPLOAD_ERR_OK) {
   $targetDir = $_SERVER['DOCUMENT_ROOT'] . '/fridge_stats/uploads/';
   $uniqueName = uniqid() . '.txt';
   $targetFile = $targetDir . $uniqueName;

    if (is_uploaded_file($_FILES['file']['tmp_name'])) {
        
        if (move_uploaded_file($_FILES['file']['tmp_name'], $targetFile)) {
            $pdo->exec("LOAD DATA LOCAL INFILE '$targetFile' INTO TABLE FridgeStats.usage_by_second
                FIELDS TERMINATED BY ','
                LINES TERMINATED BY '\n'
                IGNORE 1 ROWS
                (power, current, bus_voltage, measurement_date)");

            unlink($targetFile); // Delete the file after processing

            echo "File uploaded successfully as: " . htmlspecialchars($uniqueName);
            $output = array(
                'status' => 'success',
                'message' => 'File uploaded successfully'
            );
            echo json_encode($output);
        } else {
            $output = array(
                'status' => 'error',
                'message' => 'Error moving file.'
            );
            echo json_encode($output);
        }
    } else {
        $output = array(
            'status' => 'error',
            'message' => 'Security error: File was not uploaded via HTTP POST.'
        );
        echo json_encode($output);
    }
} else {
    $output = array(
        'status' => 'error',
        'message' => 'No file uploaded.'
    );
    echo json_encode($output);
}
?>
