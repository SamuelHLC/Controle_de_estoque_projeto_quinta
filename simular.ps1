$dir = (Get-Location).Path
$jobs = 1..5 | ForEach-Object {
    $id = $_
    Start-Job {
        Set-Location $using:dir
        docker compose run --rm -T -e MODO_AUTO=1 -e PRODUTO_ID=1 -e QUANTIDADE=2 -e USUARIO_ID=$using:id cliente-pdv
    }
}
$jobs | Wait-Job | Receive-Job
$jobs | Remove-Job
