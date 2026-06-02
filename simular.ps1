$ErrorActionPreference = 'SilentlyContinue'
$dir = (Get-Location).Path

function Get-ClientesAtivos {
    $resultado = @()
    $linhas = docker ps --format '{{.ID}}|{{.Names}}|{{.Status}}' 2>$null
    if (-not $linhas) { return $resultado }
    foreach ($linha in $linhas) {
        if ($linha -match 'cliente' -and $linha -notmatch 'servidor') {
            $p  = $linha -split '\|'
            $ip = docker inspect $p[0] --format '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' 2>$null
            $resultado += [PSCustomObject]@{
                Id     = $p[0]
                Nome   = $p[1]
                Status = $p[2]
                IP     = $ip
            }
        }
    }
    return $resultado
}

function Buscar-Produtos {
    $produtos = @()
    try {
        $linhas = docker compose run `
            --rm `
            --no-deps `
            -e MODO_LISTAR=1 `
            -e SERVIDOR_HOST=servidor-estoque `
            -e SERVIDOR_PORT=8085 `
            cliente-pdv 2>$null

        foreach ($linha in $linhas) {
            $linha = $linha.Trim()
            if ($linha -match '^\d+\|') {
                $p = $linha -split '\|'
                if ($p.Count -ge 5) {
                    $produtos += [PSCustomObject]@{
                        Id        = [int]$p[0]
                        Nome      = $p[1]
                        Categoria = $p[2]
                        Preco     = [float]$p[3]
                        Qtd       = [int]$p[4]
                    }
                }
            }
        }
    } catch {
        # Falha silenciosa
    }
    return $produtos
}

function Mostrar-Menu {
    param($ativos)
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   SIMULACAO DISTRIBUIDA DE ESTOQUE'
    if ($ativos.Count -gt 0) {
        Write-Host ('   Containers ativos: ' + $ativos.Count)
    }
    Write-Host '=========================================='
    Write-Host ' 1. Subir simulacao de usuarios'
    if ($ativos.Count -gt 0) {
        Write-Host ' 2. Acessar um container especifico'
        Write-Host ' 3. Desligar um container especifico'
        Write-Host ' 4. Desligar todos os containers'
    }
    Write-Host ' 5. Limpar containers parados'
    Write-Host ' 0. Sair'
    Write-Host '------------------------------------------'
    Write-Host -NoNewline ' Escolha: '
}

function Subir-Simulacao {
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   SUBIR SIMULACAO DE USUARIOS'
    Write-Host '=========================================='

    Write-Host -NoNewline ' Quantos usuarios deseja subir? '
    $lido = Read-Host
    $numUsuarios = 0
    if (-not [int]::TryParse($lido, [ref]$numUsuarios) -or $numUsuarios -le 0) {
        Write-Host ' Numero invalido.'
        Start-Sleep -Seconds 1
        return
    }

    $categorias = @('Eletronicos','Informatica','Eletrodomesticos','Moveis','Vestuario','Alimentos','Brinquedos','Ferramentas','Livros','Outros')

    # Verifica e sobe servidor se necessario
    $servidorRodando = docker ps --filter "name=servidor-estoque" --filter "status=running" -q 2>$null
    if (-not $servidorRodando) {
        Write-Host ' Servidor nao esta rodando. Subindo servidor...' -ForegroundColor Yellow
        docker compose up -d servidor-estoque 2>$null | Out-Null
        $tent = 0
        do {
            Start-Sleep -Seconds 2
            $servidorRodando = docker ps --filter "name=servidor-estoque" --filter "status=running" -q 2>$null
            $tent++
        } while (-not $servidorRodando -and $tent -lt 10)
        if (-not $servidorRodando) {
            Write-Host ' ERRO: Nao foi possivel subir o servidor.' -ForegroundColor Red
            Write-Host ' Execute manualmente: docker compose up --build' -ForegroundColor Yellow
            Start-Sleep -Seconds 2
            return
        }
        Write-Host ' Servidor pronto.' -ForegroundColor Green
        Start-Sleep -Seconds 1
    }

    # Busca produtos do servidor
    Write-Host ''
    Write-Host ' Buscando produtos do servidor...'
    $todosProdutos = Buscar-Produtos
    if ($todosProdutos.Count -eq 0) {
        Write-Host ' ERRO: Nenhum produto cadastrado no servidor.' -ForegroundColor Red
        Write-Host ' Cadastre produtos pelo admin antes de simular.'
        Start-Sleep -Seconds 2
        return
    }
    Write-Host (' ' + $todosProdutos.Count + ' produto(s) encontrado(s).') -ForegroundColor Green

    # Funcao interna para selecionar categoria e produto
    $selecionarProduto = {
        param($prefixo)

        $catSel = ''; $produtosSel = @()

        while ($true) {
            Write-Host ''
            Write-Host ($prefixo + 'Categorias disponiveis:')
            for ($ci = 0; $ci -lt $categorias.Count; $ci++) {
                $cat = $categorias[$ci]
                $prods = @($todosProdutos | Where-Object { $_.Categoria -eq $cat })
                if ($prods.Count -gt 0) {
                    Write-Host ($prefixo + '  ' + ($ci+1) + '. ' + $cat + ' (' + $prods.Count + ' produto(s))')
                } else {
                    Write-Host ($prefixo + '  ' + ($ci+1) + '. ' + $cat + ' (sem produtos)') -ForegroundColor DarkGray
                }
            }
            Write-Host -NoNewline ($prefixo + 'Categoria (1-10): ')
            $lc = Read-Host; $cIdx = 0
            if (-not ([int]::TryParse($lc, [ref]$cIdx)) -or $cIdx -lt 1 -or $cIdx -gt 10) {
                Write-Host ($prefixo + 'Categoria invalida.') -ForegroundColor Red; continue
            }
            $catSel = $categorias[$cIdx - 1]
            $produtosSel = @($todosProdutos | Where-Object { $_.Categoria -eq $catSel })
            if ($produtosSel.Count -eq 0) {
                Write-Host ($prefixo + 'Nenhum produto em "' + $catSel + '". Escolha outra categoria.') -ForegroundColor Yellow; continue
            }
            break
        }

        Write-Host ''
        Write-Host ($prefixo + 'Produtos em ' + $catSel + ':')
        foreach ($p in $produtosSel) {
            if ($p.Qtd -le 0) {
                Write-Host ($prefixo + '  ID: ' + $p.Id + ' | ' + $p.Nome + ' | R$ ' + ('{0:F2}' -f $p.Preco) + ' | Estoque: 0 — ZERADO') -ForegroundColor Red
            } else {
                Write-Host ($prefixo + '  ID: ' + $p.Id + ' | ' + $p.Nome + ' | R$ ' + ('{0:F2}' -f $p.Preco) + ' | Estoque: ' + $p.Qtd)
            }
        }
        Write-Host ''

        $idsValidos = $produtosSel | Select-Object -ExpandProperty Id
        $pidVal = 0
        while ($true) {
            Write-Host -NoNewline ($prefixo + 'Produto ID (' + ($idsValidos -join ', ') + '): ')
            $lp = Read-Host
            if ([int]::TryParse($lp, [ref]$pidVal) -and ($idsValidos -contains $pidVal)) { break }
            Write-Host ($prefixo + 'ID invalido. Escolha um ID da lista acima.') -ForegroundColor Red
            $pidVal = 0
        }

        $prodEscolhido = $produtosSel | Where-Object { $_.Id -eq $pidVal } | Select-Object -First 1

        if ($prodEscolhido.Qtd -le 0) {
            Write-Host ($prefixo + 'Este produto esta com estoque ZERADO. Escolha outro produto.') -ForegroundColor Red
            $pidVal = 0
            continue
        }

        $qtdVal = 0
        while ($qtdVal -le 0) {
            Write-Host -NoNewline ($prefixo + 'Quantidade (estoque: ' + $prodEscolhido.Qtd + '): ')
            $lq = Read-Host
            if (-not [int]::TryParse($lq, [ref]$qtdVal) -or $qtdVal -le 0) {
                Write-Host ($prefixo + 'Quantidade invalida.') -ForegroundColor Red; $qtdVal = 0
            }
        }

        return [PSCustomObject]@{ ProdutoId = $pidVal; Quantidade = $qtdVal; Categoria = $catSel }
    }

    Write-Host ''
    Write-Host -NoNewline ' Configurar individualmente cada usuario? (S/N): '
    $individual = Read-Host
    $configs = @()

    if ($individual -match '^[Ss]$') {
        for ($i = 1; $i -le $numUsuarios; $i++) {
            Write-Host ''
            Write-Host (' --- Usuario #' + $i + ' ---')
            $sel = & $selecionarProduto '   '
            $configs += [PSCustomObject]@{ Id = $i; ProdutoId = $sel.ProdutoId; Quantidade = $sel.Quantidade; Categoria = $sel.Categoria }
        }
    } else {
        $sel = & $selecionarProduto ' '
        for ($i = 1; $i -le $numUsuarios; $i++) {
            $configs += [PSCustomObject]@{ Id = $i; ProdutoId = $sel.ProdutoId; Quantidade = $sel.Quantidade; Categoria = $sel.Categoria }
        }
    }

    Clear-Host
    Write-Host '=========================================='
    Write-Host '   RESUMO DA SIMULACAO'
    Write-Host '=========================================='
    foreach ($c in $configs) {
        Write-Host ('  Usuario #' + $c.Id + ' | Categoria: ' + $c.Categoria + ' | Produto ID: ' + $c.ProdutoId + ' | Quantidade: ' + $c.Quantidade)
    }
    Write-Host '------------------------------------------'
    Write-Host -NoNewline ' Confirma? (S/N): '
    $confirma = Read-Host
    if ($confirma -notmatch '^[Ss]$') {
        Write-Host ' Simulacao cancelada.'
        Start-Sleep -Seconds 1
        return
    }

    Write-Host ''
    Write-Host (' Subindo ' + $numUsuarios + ' container(s) em background...')
    Write-Host ''

    # Sobe cada container
    $containerIds = @()
    foreach ($c in $configs) {
        $cid = docker compose run `
            --no-deps `
            -d `
            -e MODO_AUTO=1 `
            -e PRODUTO_ID=$($c.ProdutoId) `
            -e QUANTIDADE=$($c.Quantidade) `
            -e USUARIO_ID=$($c.Id) `
            cliente-pdv 2>$null
        if ($cid) {
            $cid = $cid.Trim()
            $containerIds += [PSCustomObject]@{ Id = $cid; UsuarioId = $c.Id }
            Write-Host ('  Usuario #' + $c.Id + ' | Container: ' + $cid.Substring(0, [Math]::Min(12, $cid.Length)))
        }
    }

    Write-Host ''
    Write-Host ' Aguardando resultado das compras...'
    Write-Host ''

    $resultados = @{}
    $pendentes  = [System.Collections.Generic.List[PSCustomObject]]($containerIds)
    $tentativas = 0
    $maxTentativas = 120

    while ($pendentes.Count -gt 0 -and $tentativas -lt $maxTentativas) {
        $tentativas++
        $idsParaRemover = @()

        foreach ($item in $pendentes) {
            $log = docker logs $item.Id 2>$null
            if ($log -match 'CONFIRMADA') {
                $resultados[$item.UsuarioId] = 'CONFIRMADA'
                $idsParaRemover += $item.Id
                Write-Host ('  Usuario #' + $item.UsuarioId + ' -> CONFIRMADA')
            } elseif ($log -match 'RECUSADA') {
                $resultados[$item.UsuarioId] = 'RECUSADA'
                $idsParaRemover += $item.Id
                Write-Host ('  Usuario #' + $item.UsuarioId + ' -> RECUSADA')
            } elseif ($log -match 'ERRO') {
                $resultados[$item.UsuarioId] = 'ERRO'
                $idsParaRemover += $item.Id
                Write-Host ('  Usuario #' + $item.UsuarioId + ' -> ERRO')
            }
        }

        foreach ($id in $idsParaRemover) {
            $item = $pendentes | Where-Object { $_.Id -eq $id } | Select-Object -First 1
            if ($item) { $pendentes.Remove($item) | Out-Null }
        }

        if ($pendentes.Count -gt 0) { Start-Sleep -Milliseconds 500 }
    }

    $confirmadas = ($resultados.Values | Where-Object { $_ -eq 'CONFIRMADA' }).Count
    $recusadas   = ($resultados.Values | Where-Object { $_ -eq 'RECUSADA'   }).Count
    $erros       = ($resultados.Values | Where-Object { $_ -eq 'ERRO'       }).Count
    $timeout     = $pendentes.Count

    Write-Host ''
    Write-Host '=========================================='
    Write-Host '   RESULTADO'
    Write-Host '=========================================='
    Write-Host ('  Usuarios subidos : ' + $numUsuarios)
    Write-Host ('  Confirmadas      : ' + $confirmadas)
    Write-Host ('  Recusadas        : ' + $recusadas)
    Write-Host ('  Erros            : ' + $erros)
    if ($timeout -gt 0) {
        Write-Host ('  Sem resposta     : ' + $timeout + ' (containers ainda ativos)')
    }
    Write-Host '=========================================='
    Write-Host ''
    Write-Host ' Containers ficam ativos no Docker.'
    Write-Host ''
    Write-Host -NoNewline ' Pressione ENTER para voltar ao menu...'
    Read-Host | Out-Null
}

function Acessar-Container {
    param($ativos)
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   ACESSAR UM CONTAINER'
    Write-Host '=========================================='
    Write-Host ''

    $arr = @($ativos)
    for ($i = 0; $i -lt $arr.Count; $i++) {
        Write-Host ('  [' + ($i+1) + '] ' + $arr[$i].Nome + '  |  IP: ' + $arr[$i].IP + '  |  ' + $arr[$i].Status)
    }

    Write-Host ''
    Write-Host ' Ao acessar o container o cliente interativo sera aberto.'
    Write-Host ' Para sair use a opcao 4 no menu do cliente.'
    Write-Host ''
    Write-Host -NoNewline ' Qual deseja acessar (0 cancela): '
    $lido = Read-Host
    $sel = 0
    if (-not [int]::TryParse($lido, [ref]$sel) -or $sel -eq 0) { return }
    if ($sel -lt 1 -or $sel -gt $arr.Count) {
        Write-Host ' Selecao invalida.'
        Start-Sleep -Seconds 1
        return
    }

    $escolhido = $arr[$sel - 1]
    Write-Host ''
    Write-Host (' Abrindo container ' + $escolhido.Nome + ' IP: ' + $escolhido.IP)
    Write-Host ' Para sair pressione 4 no menu do cliente.'
    Write-Host ''
    docker exec -it $escolhido.Id /app/build/cliente
}

function Desligar-Um {
    param($ativos)
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   DESLIGAR UM CONTAINER'
    Write-Host '=========================================='
    Write-Host ''

    $arr = @($ativos)
    for ($i = 0; $i -lt $arr.Count; $i++) {
        Write-Host ('  [' + ($i+1) + '] ' + $arr[$i].Nome + '  |  IP: ' + $arr[$i].IP + '  |  ' + $arr[$i].Status)
    }

    Write-Host ''
    Write-Host -NoNewline ' Qual deseja desligar (0 cancela): '
    $lido = Read-Host
    $sel = 0
    if (-not [int]::TryParse($lido, [ref]$sel) -or $sel -eq 0) { return }
    if ($sel -lt 1 -or $sel -gt $arr.Count) {
        Write-Host ' Selecao invalida.'
        Start-Sleep -Seconds 1
        return
    }

    $escolhido = $arr[$sel - 1]
    docker rm -f $escolhido.Id 2>$null | Out-Null
    Write-Host ''
    Write-Host (' Container ' + $escolhido.Nome + ' IP: ' + $escolhido.IP + ' encerrado e removido.')
    Start-Sleep -Seconds 1
}

function Desligar-Todos {
    param($ativos)
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   DESLIGAR TODOS OS CONTAINERS'
    Write-Host '=========================================='
    Write-Host ''
    Write-Host (' Serao encerrados e removidos ' + $ativos.Count + ' container(s):')
    foreach ($c in $ativos) {
        Write-Host ('  - ' + $c.Nome + '  |  IP: ' + $c.IP)
    }
    Write-Host ''
    Write-Host -NoNewline ' Confirma? (S/N): '
    $confirma = Read-Host
    if ($confirma -notmatch '^[Ss]$') {
        Write-Host ' Cancelado.'
        Start-Sleep -Seconds 1
        return
    }

    $count = 0
    foreach ($c in $ativos) {
        docker rm -f $c.Id 2>$null | Out-Null
        Write-Host (' Container ' + $c.Nome + ' IP: ' + $c.IP + ' encerrado e removido.')
        $count++
    }

    Write-Host ''
    Write-Host (' ' + $count + ' container(s) encerrado(s) e removido(s).')
    Start-Sleep -Seconds 1
}

function Limpar-Parados {
    Clear-Host
    Write-Host '=========================================='
    Write-Host '   LIMPAR CONTAINERS PARADOS'
    Write-Host '=========================================='
    Write-Host ''
    Write-Host ' Remove todos os containers parados (nao afeta os ativos).'
    Write-Host ''
    Write-Host -NoNewline ' Confirma? (S/N): '
    $confirma = Read-Host
    if ($confirma -notmatch '^[Ss]$') {
        Write-Host ' Cancelado.'
        Start-Sleep -Seconds 1
        return
    }
    docker container prune -f 2>$null | Out-Null
    Write-Host ' Containers parados removidos.'
    Start-Sleep -Seconds 1
}

# ── Loop principal ────────────────────────────────────────────────────
$sair = $false
while (-not $sair) {
    $ativos = @(Get-ClientesAtivos)
    Mostrar-Menu -ativos $ativos
    $op = Read-Host

    switch ($op) {
        '1' { Subir-Simulacao }
        '2' {
            if ($ativos.Count -gt 0) { Acessar-Container -ativos $ativos }
            else { Write-Host ' Nenhum container ativo.'; Start-Sleep -Seconds 1 }
        }
        '3' {
            if ($ativos.Count -gt 0) { Desligar-Um -ativos $ativos }
            else { Write-Host ' Nenhum container ativo.'; Start-Sleep -Seconds 1 }
        }
        '4' {
            if ($ativos.Count -gt 0) { Desligar-Todos -ativos $ativos }
            else { Write-Host ' Nenhum container ativo.'; Start-Sleep -Seconds 1 }
        }
        '5' { Limpar-Parados }
        '0' { $sair = $true }
        default { Write-Host ' Opcao invalida.'; Start-Sleep -Seconds 1 }
    }
}