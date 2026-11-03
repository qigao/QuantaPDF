$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
# Same immutable ccache assets as SaltsCore; file transfers use curl only.
$platform, $digest = switch ("$env:RUNNER_OS-$env:RUNNER_ARCH") {
    'Linux-X64' { 'linux-x86_64-glibc', 'b1f7fbb60ee24b80a6abae09ec4fcc6554fbf98c933c9bd25a3f53ab685fc00b' }
    'macOS-ARM64' { 'darwin', 'cf240009630b605b775c7c5c7b7105a3b196c79f31af322c1557969aa28003fc' }
    'Windows-X64' { 'windows-x86_64', '6993ce645e53192cec592ee23186200b574ac4c59097ecde8d9cfd681b47ae84' }
    default { throw "Unsupported ccache host: $env:RUNNER_OS-$env:RUNNER_ARCH" }
}
$name = "ccache-$env:CCACHE_VERSION-$platform"
$extension = if ($IsWindows) { 'zip' } else { 'tar.gz' }
$archive = Join-Path $env:RUNNER_TEMP "$name.$extension"
curl --fail --location --silent --show-error `
    "https://github.com/ccache/ccache/releases/download/v$env:CCACHE_VERSION/$name.$extension" `
    --output $archive
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $archive).Hash -ne $digest) {
    throw 'ccache archive SHA256 mismatch'
}
if ($IsWindows) {
    Expand-Archive -LiteralPath $archive -DestinationPath $env:RUNNER_TEMP
} else {
    tar -xf $archive -C $env:RUNNER_TEMP
}
(Join-Path $env:RUNNER_TEMP $name) >> $env:GITHUB_PATH
