<?php
require 'db.php';

$power = $_GET["power"];
$current = $_GET["current"];
$bus_voltage = $_GET["bus_voltage"];
$measurement_date = $_GET["measurement_date"];
try{
   $stmt = $pdo->prepare("insert into FridgeStats.usage_by_second(power, current, bus_voltage, measurement_date)
      values(:power, :current, :bus_voltage, :measurement_date)");
   $stmt->bindParam(":power", $power);
   $stmt->bindParam(":current", $current);
   $stmt->bindParam(":bus_voltage", $bus_voltage);
   $stmt->bindParam(":measurement_date", $measurement_date);
   $stmt->execute();
   $output = array(
      'status' => 'success',
      'message' => 'Data inserted successfully'
   );
   echo json_encode($output);
}catch(Exception $e){
   echo $e->getMessage();
}
?>
