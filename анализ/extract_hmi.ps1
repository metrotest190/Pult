param(
    [string]$Path = "C:\Users\190\Desktop\Пульт\Большой\Screen\V1.4 .0.HMI",
    [string]$Out = "C:\Users\190\Desktop\new\workspace\Pult_Encod_Koda\анализ\hmi_script_dump.txt",
    [long]$From = 7354000
)
$ErrorActionPreference = "Stop"
$b = [System.IO.File]::ReadAllBytes($Path)
$sb = New-Object System.Text.StringBuilder
$cur = New-Object System.Text.StringBuilder
$curStart = 0L
$minRun = 2

function Add-Run {
    param($sb, $cur, $curStart, $minRun)
    if ($cur.Length -ge $minRun) {
        [void]$sb.AppendLine(("{0}`t{1}" -f $curStart, $cur.ToString()))
    }
    [void]$cur.Clear()
}

for ($i = $From; $i -lt $b.Length; $i++) {
    $byte = $b[$i]
    $isText = ($byte -eq 9) -or ($byte -eq 10) -or ($byte -eq 13) -or ($byte -ge 32 -and $byte -le 126)
    if ($isText) {
        if ($cur.Length -eq 0) { $script:curStart = $i }
        if ($byte -eq 9) { [void]$cur.Append("`t") }
        elseif ($byte -eq 10 -or $byte -eq 13) { [void]$cur.Append(" ") }
        else { [void]$cur.Append([char]$byte) }
    } else {
        Add-Run $sb $cur $curStart $minRun
    }
}
Add-Run $sb $cur $curStart $minRun

[System.IO.File]::WriteAllText($Out, $sb.ToString(), [System.Text.Encoding]::UTF8)
Write-Host ("Runs written to {0}" -f $Out)
