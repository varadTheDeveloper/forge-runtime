$dest = "C:\Forge\bin"

New-Item -ItemType Directory -Force -Path $dest | Out-Null

Copy-Item `
    ".\obj-spider\dist\bin\forge.exe" `
    "$dest\forge.exe" `
    -Force

Write-Host "Forge installed to $dest"