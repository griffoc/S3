<?php
$host = "localhost";
$user = "craig";
$password = "nikki1";
$dbname = "FridgeStats";

try {
    $options = [
        PDO::MYSQL_ATTR_LOCAL_INFILE => true
    ];
    $pdo = new PDO("mysql:host=$host;dbname=$dbname", $user, $password, $options);
    $pdo->setAttribute(PDO::ATTR_ERRMODE, PDO::ERRMODE_EXCEPTION);
} catch (PDOException $e) {
    die("Connection failed: " . $e->getMessage());
}
?>