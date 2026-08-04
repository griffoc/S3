<?php
// /var/www/html/api/api.php
 
declare(strict_types=1);
 
require_once __DIR__ . '/../includes/db.php';
 
header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: GET, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');
 
if ($_SERVER['REQUEST_METHOD'] === 'OPTIONS') {
    http_response_code(204);
    exit;
}
 
if ($_SERVER['REQUEST_METHOD'] !== 'GET') {
    http_response_code(405);
    echo json_encode(['success' => false, 'error' => 'Method not allowed']);
    exit;
}
 
$path = parse_url($_SERVER['REQUEST_URI'], PHP_URL_PATH);
$path = rtrim($path, '/');
 
$segments = explode('/', $path);
$endpoint = end($segments);
 
try {
    $pdo = getDbConnection();
 
    match ($endpoint) {
        'readings' => handleReadings($pdo),
        'range'    => handleRange($pdo),
        'latest'   => handleLatest($pdo),
        default    => throw new \RuntimeException('Unknown endpoint'),
    };
} catch (\RuntimeException $e) {
    http_response_code(404);
    echo json_encode(['success' => false, 'error' => $e->getMessage()]);
} catch (\Throwable $e) {
    http_response_code(500);
    echo json_encode(['success' => false, 'error' => 'Internal server error']);
}

function handleReadings(PDO $pdo): void
{
    $limit = isset($_GET['limit'])
        ? min(max((int) $_GET['limit'], 1), 1000)
        : 100;
 
    $stmt = $pdo->prepare(
        'SELECT
            CAST(AVG(CASE WHEN current <= 0 THEN 0 ELSE power END) AS DECIMAL(10,2)) AS power,
            CAST(AVG(CASE WHEN current <= 0 THEN 0 ELSE current END) AS DECIMAL(10,2)) AS current,
            CAST(AVG(CASE WHEN current <= 0 THEN NULL ELSE bus_voltage END) AS DECIMAL(10,2)) AS bus_voltage,
            DATE_FORMAT(measurement_date, \'%Y-%m-%d %H:00:00\') AS measurement_date
            FROM usage_by_second
            GROUP BY DATE_FORMAT(measurement_date, \'%Y-%m-%d %H:00:00\')
        UNION
            SELECT power, current, bus_voltage, measurement_date
                FROM usage_by_hour
        ORDER BY measurement_date DESC
        LIMIT :limit'
    );
    $stmt->bindValue(':limit', $limit, PDO::PARAM_INT);
    $stmt->execute();
 
    $data = $stmt->fetchAll();
    $data = array_reverse($data);
 
    echo json_encode([
        'success' => true,
        'data'    => $data,
        'count'   => count($data),
        'query'   => [
            'from'  => null,
            'to'    => null,
            'limit' => $limit,
        ],
    ]);
}

function handleRange(PDO $pdo): void
{
    $from = $_GET['from'] ?? null;
    $to   = $_GET['to'] ?? null;
 
    if ($from === null || $to === null) {
        http_response_code(400);
        echo json_encode([
            'success' => false,
            'error'   => 'Both "from" and "to" parameters are required.',
        ]);
        return;
    }
 
    $fromDt = \DateTimeImmutable::createFromFormat('Y-m-d H:i:s', $from);
    $toDt   = \DateTimeImmutable::createFromFormat('Y-m-d H:i:s', $to);
 
    if ($fromDt === false || $toDt === false) {
        http_response_code(400);
        echo json_encode([
            'success' => false,
            'error'   => 'Invalid date format. Use YYYY-MM-DD HH:MM:SS.',
        ]);
        return;
    }
 
    $stmt = $pdo->prepare(
        'SELECT
            power,
            current,
            bus_voltage,
            measurement_date
         FROM usage_by_hour
         WHERE measurement_date BETWEEN :from AND :to
         ORDER BY measurement_date ASC'
    );
    $stmt->bindValue(':from', $from, PDO::PARAM_STR);
    $stmt->bindValue(':to', $to, PDO::PARAM_STR);
    $stmt->execute();
 
    $data = $stmt->fetchAll();
 
    echo json_encode([
        'success' => true,
        'data'    => $data,
        'count'   => count($data),
        'query'   => [
            'from'  => $from,
            'to'    => $to,
            'limit' => null,
        ],
    ]);
}

function handleLatest(PDO $pdo): void
{
    $stmt = $pdo->query(
        'SELECT power, current, bus_voltage, measurement_date
         FROM usage_by_second
         ORDER BY measurement_date DESC
         LIMIT 1'
    );
 
    $data = $stmt->fetch();
 
    if ($data === false) {
        echo json_encode([
            'success' => true,
            'data'    => null,
            'count'   => 0,
        ]);
        return;
    }
 
    echo json_encode([
        'success' => true,
        'data'    => $data,
        'count'   => 1,
    ]);
}
