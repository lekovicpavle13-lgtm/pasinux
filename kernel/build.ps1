 $ErrorActionPreference = "Stop"
if (!(Test-Path "build")) { New-Item -ItemType Directory -Path "build" | Out-Null }
Remove-Item -Path "build\*.o" -ErrorAction SilentlyContinue
Remove-Item -Path "build\*.elf" -ErrorAction SilentlyContinue
Remove-Item -Path "build\*.bin" -ErrorAction SilentlyContinue
Remove-Item -Path "build\*.img" -ErrorAction SilentlyContinue

# USE THE NEW MSYS2 32-BIT TOOLS!
 $CC = "C:\msys64\mingw32\bin\gcc.exe"
 $LD = "C:\msys64\mingw32\bin\ld.exe"
 $OBJCOPY = "C:\msys64\mingw32\bin\objcopy.exe"
 $NASM = "C:\Program Files\NASM\nasm.exe"

Write-Host "Compiling C sources..." -ForegroundColor Cyan
& $CC -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -Wall -Wextra -c kernel.c -o build/kernel.o
& $CC -m32 -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector -Wall -Wextra -c serial.c -o build/serial.o

Write-Host "Assembling ASM sources..." -ForegroundColor Cyan
& $NASM -f elf32 entry.asm -o build/entry.o
& $NASM -f elf32 isr.asm -o build/isr.o

Write-Host "Linking kernel..." -ForegroundColor Cyan
& $LD -m elf_i386 -T linker.ld -nostdlib -o build/kernel.elf build/kernel.o build/serial.o build/entry.o build/isr.o

Write-Host "Extracting raw binary..." -ForegroundColor Cyan
& $OBJCOPY -O binary build/kernel.elf build/kernel.bin

 $kernelSize = (Get-Item build/kernel.bin).Length
 $kernelSectors = [math]::Ceiling($kernelSize / 512)
if ($kernelSectors -lt 1) { $kernelSectors = 1 }

Write-Host "Assembling bootloader..." -ForegroundColor Cyan
& $NASM -f bin -dKERNEL_SECTORS=$kernelSectors -o build/boot.bin boot.asm

Write-Host "Creating disk.img..." -ForegroundColor Cyan
 $bootBin = [System.IO.File]::ReadAllBytes("build\boot.bin")
 $kernelBin = [System.IO.File]::ReadAllBytes("build\kernel.bin")
 $diskImg = New-Object byte[] ($bootBin.Length + $kernelBin.Length)
[System.Array]::Copy($bootBin, 0, $diskImg, 0, $bootBin.Length)
[System.Array]::Copy($kernelBin, 0, $diskImg, $bootBin.Length, $kernelBin.Length)
[System.IO.File]::WriteAllBytes("build\disk.img", $diskImg)

Write-Host "SUCCESS: build\disk.img created!" -ForegroundColor Green
