<?php
// /var/www/html/includes/db.php
 
declare(strict_types=1);
 
function getDbConnection(): PDO
{
    static $pdo = null;
 
    if ($pdo === null) {
        $host = 'localhost';
        $db   = 'FridgeStats';
        $user = 'craig';
        $pass = 'nikki1';
        $charset = 'utf8mb4';
 
        $dsn = "mysql:host={$host};dbname={$db};charset={$charset}";
 
        $options = [
            PDO::ATTR_ERRMODE            => PDO::ERRMODE_EXCEPTION,
            PDO::ATTR_DEFAULT_FETCH_MODE => PDO::FETCH_ASSOC,
            PDO::ATTR_EMULATE_PREPARES   => false,
        ];
 
        $pdo = new PDO($dsn, $user, $pass, $options);
    }
 
    return $pdo;
}
