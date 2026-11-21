# @file browserClickTest.ps1
# Click-tests the dashboard in a real browser: starts an invisible (headless) Chrome with a fresh profile
# in %TEMP% and its DevTools port on 127.0.0.1 only, clicks "Run all steps" in the guided demo, runs the
# load generator, the request tester, the theme button and an event filter, takes desktop / phone /
# dark-theme screenshots, reports console errors and horizontal overflow, then closes that Chrome and
# deletes its profile. Installs nothing and changes no settings.
#
# Needs: Windows with Google Chrome, and the compose cluster running with the dashboard on
# http://localhost:8090 (ADMIN_ENABLED=1, the compose default). From WSL:
#   powershell.exe -NoProfile -ExecutionPolicy Bypass -File "$(wslpath -w dashboard/tests/browserClickTest.ps1)" \
#       -OutDir "$(wslpath -w /tmp)"
# Pass: every guided-demo step "passed", 0 console errors, no horizontal overflow.
param(
    [Parameter(Mandatory = $true)][string]$OutDir,
    [string]$Chrome = 'C:\Program Files\Google\Chrome\Application\chrome.exe'
)

$ErrorActionPreference = 'Stop'
$port = 9333
$profileDir = Join-Path $env:TEMP ('outpostDemo' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$console = New-Object System.Collections.ArrayList
$socket = $null
$browser = $null
$script:nextId = 0

function Receive-Message {
    $buffer = New-Object byte[] 1048576
    $stream = New-Object System.IO.MemoryStream
    do {
        $segment = New-Object System.ArraySegment[byte] -ArgumentList @(, $buffer)
        $result = $socket.ReceiveAsync($segment, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
        $stream.Write($buffer, 0, $result.Count)
    } while (-not $result.EndOfMessage)
    return [Text.Encoding]::UTF8.GetString($stream.ToArray())
}

# Sends one DevTools command and returns its raw JSON answer; console events seen meanwhile are kept.
function Invoke-Cdp([string]$method, [hashtable]$params = @{}) {
    $script:nextId += 1
    $id = $script:nextId
    $json = @{ id = $id; method = $method; params = $params } | ConvertTo-Json -Depth 10 -Compress
    $bytes = [Text.Encoding]::UTF8.GetBytes($json)
    $segment = New-Object System.ArraySegment[byte] -ArgumentList @(, $bytes)
    [void]$socket.SendAsync($segment, [Net.WebSockets.WebSocketMessageType]::Text, $true, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    $answer = $null
    while ($null -eq $answer) {
        $message = Receive-Message
        if ($message -match '^\{"id":(\d+)' -and [int]$Matches[1] -eq $id) {
            $answer = $message
        } elseif ($message -match '"method":"(Runtime\.exceptionThrown|Log\.entryAdded|Runtime\.consoleAPICalled)"') {
            if ($message -match '"(level|type)":"(error|warning|assert)"' -or $Matches[1] -eq 'Runtime.exceptionThrown') {
                [void]$console.Add($message.Substring(0, [Math]::Min(400, $message.Length)))
            }
        }
    }
    return $answer
}

# Evaluates a JavaScript expression in the page and returns its value as a string.
function Invoke-Js([string]$expression) {
    $answer = Invoke-Cdp 'Runtime.evaluate' @{ expression = $expression; returnByValue = $true; awaitPromise = $true }
    return (($answer | ConvertFrom-Json).result.result.value)
}

function Save-Screenshot([string]$name, [int]$width, [bool]$mobile) {
    [void](Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{ width = $width; height = 900; deviceScaleFactor = 1; mobile = $mobile })
    Start-Sleep -Milliseconds 400
    $height = [int](Invoke-Js 'Math.ceil(document.documentElement.scrollHeight)')
    [void](Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{ width = $width; height = [Math]::Min($height, 8000); deviceScaleFactor = 1; mobile = $mobile })
    Start-Sleep -Milliseconds 400
    $answer = Invoke-Cdp 'Page.captureScreenshot' @{ format = 'png' }
    if ($answer -match '"data":"([^"]+)"') {
        [IO.File]::WriteAllBytes((Join-Path $OutDir "$name.png"), [Convert]::FromBase64String($Matches[1]))
    }
    $overflow = Invoke-Js 'document.documentElement.scrollWidth > document.documentElement.clientWidth'
    Write-Output ("screenshot {0}: {1}x{2}, horizontal overflow: {3}" -f $name, $width, $height, $overflow)
}

try {
    $browser = Start-Process -FilePath $Chrome -PassThru -ArgumentList @(
        '--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
        "--remote-debugging-port=$port", '--remote-debugging-address=127.0.0.1',
        "--user-data-dir=$profileDir", 'about:blank')
    $target = $null
    for ($i = 0; $i -lt 40 -and $null -eq $target; $i++) {
        Start-Sleep -Milliseconds 250
        try {
            $targets = Invoke-RestMethod "http://127.0.0.1:$port/json/list"
            $target = $targets | Where-Object { $_.type -eq 'page' } | Select-Object -First 1
        } catch {
            $target = $null
        }
    }
    if ($null -eq $target) {
        throw 'Chrome did not open its DevTools port'
    }
    $socket = New-Object System.Net.WebSockets.ClientWebSocket
    [void]$socket.ConnectAsync([Uri]$target.webSocketDebuggerUrl, [Threading.CancellationToken]::None).GetAwaiter().GetResult()
    foreach ($domain in 'Page.enable', 'Runtime.enable', 'Log.enable') {
        [void](Invoke-Cdp $domain)
    }
    [void](Invoke-Cdp 'Emulation.setDeviceMetricsOverride' @{ width = 1280; height = 900; deviceScaleFactor = 1; mobile = $false })
    [void](Invoke-Cdp 'Page.navigate' @{ url = 'http://localhost:8090/' })
    Start-Sleep -Seconds 3

    # Guided demo: click "Run all steps" and wait until it finishes (the button is enabled again).
    [void](Invoke-Js "document.getElementById('demoRunAll').click(); true")
    $started = Get-Date
    Start-Sleep -Seconds 1
    while ((Invoke-Js "String(document.getElementById('demoRunAll').disabled)") -eq 'true' -and ((Get-Date) - $started).TotalSeconds -lt 120) {
        Start-Sleep -Seconds 1
    }
    Write-Output ("guided demo finished after {0:N1} s" -f ((Get-Date) - $started).TotalSeconds)
    Write-Output (Invoke-Js "[...document.querySelectorAll('.demoStep')].map((row, i) => (i + 1) + ' ' + row.dataset.status + ': ' + row.querySelector('.demoResult').textContent).join('\n')")
    Write-Output ('flow caption: ' + (Invoke-Js "document.getElementById('flowCaption').textContent"))
    Save-Screenshot 'demoDesktopLight' 1280 $false

    # Load generator with a small run.
    [void](Invoke-Js "document.getElementById('loadCount').value = 200; document.getElementById('loadStart').click(); true")
    $started = Get-Date
    Start-Sleep -Seconds 1
    while ((Invoke-Js "String(document.getElementById('loadStart').disabled)") -eq 'true' -and ((Get-Date) - $started).TotalSeconds -lt 60) {
        Start-Sleep -Milliseconds 500
    }
    Write-Output ('load generator: ' + (Invoke-Js "document.getElementById('loadProgress').textContent + ' | ' + [...document.querySelectorAll('#loadNumbers div')].map(d => d.textContent).join(' ')"))

    # Request tester: purge, then a GET of the same key.
    $tester = "(async () => { const form = document.getElementById('requestForm'); document.getElementById('reqKey').value = 'tester-key';" +
        " for (const action of ['PUT', 'GET', 'PURGE', 'GET']) { form.querySelector('input[value=' + action + ']').click(); form.requestSubmit();" +
        " await new Promise(r => setTimeout(r, 700)); out.push(action + ' -> ' + document.getElementById('reqSummary').textContent); } return out.join(' | '); })()"
    Write-Output ('request tester: ' + (Invoke-Js ("const out = []; " + $tester)))

    # Theme button and filter chip.
    Write-Output ('theme button: ' + (Invoke-Js "const b = document.getElementById('themeToggle'); b.click(); const first = document.documentElement.dataset.theme; b.click(); const second = document.documentElement.dataset.theme; b.click(); first + ' -> ' + second + ' -> ' + (document.documentElement.dataset.theme || 'auto')"))
    Write-Output ('breaker filter: ' + (Invoke-Js "document.querySelector('#eventFilters [data-type=breaker]').click(); const types = new Set([...document.querySelectorAll('#events tr')].map(r => r.children[2] ? r.children[2].textContent : 'empty')); document.querySelector('#eventFilters [data-type=all]').click(); [...types].join(',')"))

    Save-Screenshot 'phoneLight' 390 $true
    [void](Invoke-Cdp 'Emulation.setEmulatedMedia' @{ features = @(@{ name = 'prefers-color-scheme'; value = 'dark' }) })
    Save-Screenshot 'desktopDark' 1280 $false
    Save-Screenshot 'phoneDark' 390 $true

    Write-Output ("console errors/warnings/exceptions: {0}" -f $console.Count)
    foreach ($line in $console) {
        Write-Output ('  ' + $line)
    }
} finally {
    if ($null -ne $socket) {
        try { [void](Invoke-Cdp 'Browser.close') } catch { }
        $socket.Dispose()
    }
    Start-Sleep -Seconds 2
    Get-CimInstance Win32_Process -Filter "Name='chrome.exe'" |
        Where-Object { $_.CommandLine -like "*$profileDir*" } |
        ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 1
    Remove-Item -LiteralPath $profileDir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Output ("cleanup: chrome stopped, profile removed: {0}" -f (-not (Test-Path $profileDir)))
}
