<#
ClassFlow 安装脚本（绿色安装，无需管理员权限）

作用：
    把当前目录下的 ClassFlow 程序文件复制到当前用户目录，
    并创建桌面快捷方式与开始菜单项。

用法：
    双击同目录下的 install.bat，或在 PowerShell 中执行本脚本。

Remark：
    安装位置选在 %LOCALAPPDATA%\Programs\ClassFlow，属用户目录，
    不触发 UAC 提权；程序的自动恢复文件 .classflow\workspace.dat 也写在这里，
    不会有权限问题。
#>

$ErrorActionPreference = 'Stop'

# ---- 路径 ----
$src = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
$app = Join-Path $env:LOCALAPPDATA 'Programs\ClassFlow'
$exe = Join-Path $app 'ClassFlow.exe'

Write-Host ''
Write-Host '  ClassFlow 0.5.0 安装程序' -ForegroundColor Cyan
Write-Host '  ----------------------------------------' -ForegroundColor DarkGray
Write-Host ''

# ---- 检查源文件是否完整 ----
if (-not (Test-Path (Join-Path $src 'ClassFlow.exe'))) {
    Write-Host '  [错误] 当前目录下找不到 ClassFlow.exe。' -ForegroundColor Red
    Write-Host '         请先把压缩包完整解压，再运行安装脚本。' -ForegroundColor Red
    Write-Host ''
    Read-Host '按回车键退出'
    exit 1
}

# ---- 程序运行中则拒绝安装 ----
if (Get-Process -Name 'ClassFlow' -ErrorAction SilentlyContinue) {
    Write-Host '  [错误] ClassFlow 正在运行，请先关闭程序再安装。' -ForegroundColor Red
    Write-Host ''
    Read-Host '按回车键退出'
    exit 1
}

# ---- 复制文件 ----
Write-Host "  安装位置：$app"
New-Item -ItemType Directory -Force -Path $app | Out-Null

$srcFull = (Resolve-Path -LiteralPath $src).Path
$appFull = (Resolve-Path -LiteralPath $app).Path

if ($srcFull -eq $appFull) {
    Write-Host '  已在安装目录内，跳过文件复制。' -ForegroundColor DarkGray
} else {
    # 安装包自身的脚本不复制进安装目录
    $skip = @('install.bat', 'uninstall.bat', 'install.ps1', 'uninstall.ps1', 'README.txt')
    Get-ChildItem -LiteralPath $src -Force |
        Where-Object { $skip -notcontains $_.Name } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $app -Recurse -Force }
    Write-Host '  程序文件复制完成。' -ForegroundColor DarkGray
}

# ---- 创建快捷方式 ----
$shell    = New-Object -ComObject WScript.Shell
$desktop  = [Environment]::GetFolderPath('Desktop')
$programs = [Environment]::GetFolderPath('Programs')
$menuDir  = Join-Path $programs 'ClassFlow'
New-Item -ItemType Directory -Force -Path $menuDir | Out-Null

function New-ClassFlowShortcut {
    param([string]$LinkPath)

    $lnk = $shell.CreateShortcut($LinkPath)
    $lnk.TargetPath       = $exe
    $lnk.WorkingDirectory = $app
    $lnk.IconLocation     = "$exe,0"
    $lnk.Description      = 'ClassFlow 高校排课系统'
    $lnk.Save()
}

New-ClassFlowShortcut (Join-Path $desktop 'ClassFlow.lnk')
New-ClassFlowShortcut (Join-Path $menuDir 'ClassFlow.lnk')

Write-Host '  已创建桌面快捷方式与开始菜单项。' -ForegroundColor DarkGray
Write-Host ''
Write-Host '  [完成] 安装成功，可从桌面快捷方式启动。' -ForegroundColor Green
Write-Host ''
Write-Host '  卸载方法：运行本目录下的 uninstall.bat' -ForegroundColor DarkGray
Write-Host ''
Read-Host '按回车键退出'
