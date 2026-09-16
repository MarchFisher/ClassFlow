<#
ClassFlow 卸载脚本

作用：
    删除 ClassFlow 的安装目录、桌面快捷方式与开始菜单项。
    运行前会二次确认；可选择是否保留自动恢复文件。

用法：
    双击同目录下的 uninstall.bat，或在 PowerShell 中执行本脚本。

Remark：
    自动恢复文件 .classflow\workspace.dat 记录着上次未保存的工作区，
    删除后"关闭再打开回到现场"的能力会一并消失，故单独询问。
    用户自己另存的项目文件（Ctrl+S 保存的 .csv 快照）不在安装目录内，不受影响。
#>

$ErrorActionPreference = 'Stop'

$app = Join-Path $env:LOCALAPPDATA 'Programs\ClassFlow'

Write-Host ''
Write-Host '  ClassFlow 卸载程序' -ForegroundColor Cyan
Write-Host '  ----------------------------------------' -ForegroundColor DarkGray
Write-Host ''

# ---- 是否已安装 ----
if (-not (Test-Path -LiteralPath $app)) {
    Write-Host "  未在 $app 找到安装的程序，无需卸载。" -ForegroundColor Yellow
    Write-Host ''
    Read-Host '按回车键退出'
    exit 0
}

# ---- 程序运行中则拒绝卸载 ----
if (Get-Process -Name 'ClassFlow' -ErrorAction SilentlyContinue) {
    Write-Host '  [错误] ClassFlow 正在运行，请先关闭程序再卸载。' -ForegroundColor Red
    Write-Host ''
    Read-Host '按回车键退出'
    exit 1
}

# ---- 二次确认 ----
Write-Host "  将删除：$app"
Write-Host '           桌面快捷方式、开始菜单项'
Write-Host ''
$answer = Read-Host '  确认卸载？输入 Y 继续，其它任意键取消'
if ($answer -notmatch '^[Yy]') {
    Write-Host ''
    Write-Host '  已取消，未做任何改动。' -ForegroundColor Yellow
    Write-Host ''
    Read-Host '按回车键退出'
    exit 0
}

# ---- 自动恢复文件单独询问 ----
$workspace = Join-Path $app '.classflow'
$keepWorkspace = $false
if (Test-Path -LiteralPath $workspace) {
    Write-Host ''
    Write-Host '  检测到自动恢复文件 .classflow\workspace.dat，' -ForegroundColor DarkGray
    Write-Host '  其中保存着上次退出时的工作区。' -ForegroundColor DarkGray
    $keep = Read-Host '  是否保留？输入 Y 保留，其它任意键一并删除'
    if ($keep -match '^[Yy]') {
        $keepWorkspace = $true
        $backup = Join-Path ([Environment]::GetFolderPath('Desktop')) 'ClassFlow-工作区备份'
        New-Item -ItemType Directory -Force -Path $backup | Out-Null
        Copy-Item -LiteralPath (Join-Path $workspace 'workspace.dat') -Destination $backup -Force
        Write-Host "  已备份到：$backup" -ForegroundColor Green
    }
}

# ---- 删除快捷方式 ----
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Desktop')) 'ClassFlow.lnk') `
    -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath (Join-Path ([Environment]::GetFolderPath('Programs')) 'ClassFlow') `
    -Recurse -Force -ErrorAction SilentlyContinue

# ---- 删除安装目录 ----
if ($keepWorkspace) {
    Get-ChildItem -LiteralPath $app -Force |
        Where-Object { $_.Name -ne '.classflow' } |
        ForEach-Object { Remove-Item -LiteralPath $_.FullName -Recurse -Force }
    Write-Host '  程序文件已删除，保留 .classflow 工作区目录。' -ForegroundColor DarkGray
} else {
    Remove-Item -LiteralPath $app -Recurse -Force
}

Write-Host ''
Write-Host '  [完成] 卸载完毕。' -ForegroundColor Green
Write-Host ''
Read-Host '按回车键退出'
