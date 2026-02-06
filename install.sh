#!/bin/bash
#
# Script de instalación para cFtpfs
# Sistema de archivos FUSE para FTP

set -e

# Colores para output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Función para imprimir mensajes
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[ADVERTENCIA]${NC} $1"
}

# Detectar sistema operativo
OS=""
if [ -f /etc/os-release ]; then
    . /etc/os-release
    OS=$ID
fi

# Verificar si se ejecuta como root (solo para algunas operaciones)
if [ "$EUID" -eq 0 ]; then
    SUDO=""
else
    SUDO="sudo"
fi

print_info "Instalador de cFtpfs v1.0.0"
print_info "Sistema operativo detectado: $OS"

# Instalar dependencias
print_info "Instalando dependencias..."

case $OS in
    ubuntu|debian)
        $SUDO apt-get update
        $SUDO apt-get install -y libfuse3-dev libcurl4-openssl-dev build-essential pkg-config
        ;;
    fedora|rhel|centos)
        $SUDO dnf install -y fuse3-devel libcurl-devel gcc make pkgconfig
        ;;
    arch|manjaro)
        $SUDO pacman -S --needed fuse3 curl gcc make pkgconf
        ;;
    *)
        print_warn "Sistema operativo no reconocido. Por favor instale manualmente:"
        print_warn "  - libfuse3-dev (o fuse3-devel)"
        print_warn "  - libcurl4-openssl-dev (o libcurl-devel)"
        print_warn "  - build-essential (o gcc, make)"
        exit 1
        ;;
esac

# Compilar
print_info "Compilando cFtpfs..."
make clean
make

# Instalar binario
print_info "Instalando binario..."
$SUDO cp cftpfs /usr/local/bin/
$SUDO chmod 755 /usr/local/bin/cftpfs

# Crear directorio de man pages
$SUDO mkdir -p /usr/local/share/man/man1

# Crear man page
print_info "Instalando página de manual..."
$SUDO tee /usr/local/share/man/man1/cftpfs.1 > /dev/null <<'EOF'
.TH CFTPFS 1 "2024-02-05" "cFtpfs 1.0.0" "Sistema de archivos FUSE para FTP"
.SH NOMBRE
cftpfs \- Monta servidores FTP como sistemas de archivos locales
.SH SINOPSIS
.B cftpfs
[\fIopciones\fR] \fIhost\fR \fImountpoint\fR
.SH DESCRIPCIÓN
.B cftpfs
es un sistema de archivos en espacio de usuario (FUSE) que permite montar
servidores FTP como directorios locales en el sistema de archivos.
.SH OPCIONES
.TP
.BR \-p ", " \-\-port =\fIPUERTO\fR
Puerto FTP (default: 21)
.TP
.BR \-u ", " \-\-user =\fIUSUARIO\fR
Usuario FTP (default: anonymous)
.TP
.BR \-P ", " \-\-password =\fIPASS\fR
Contraseña FTP
.TP
.BR \-e ", " \-\-encoding =\fIENC\fR
Codificación (default: utf-8)
.TP
.BR \-d ", " \-\-debug
Modo debug con logs detallados
.TP
.BR \-f ", " \-\-foreground
Ejecutar en primer plano
.TP
.BR \-h ", " \-\-help
Mostrar ayuda
.SH EJEMPLOS
.TP
Montar servidor FTP:
.B cftpfs ftp.example.com /mnt/ftp -u usuario -P password -f
.TP
Montar en background:
.B cftpfs ftp.example.com /mnt/ftp -u usuario -P password
.TP
Desmontar:
.B fusermount -u /mnt/ftp
.SH VÉASE TAMBIÉN
.BR fusermount (1),
.BR ftp (1),
.BR curl (1)
.SH AUTOR
Implementación en C basada en PyFtpfs.
.SH LICENCIA
Apache 2.0
EOF

$SUDO mandb 2>/dev/null || true

# Crear script de desinstalación
print_info "Creando script de desinstalación..."
cat > /tmp/uninstall_cftpfs.sh << 'EOFUN'
#!/bin/bash
set -e

SUDO=""
if [ "$EUID" -ne 0 ]; then
    SUDO="sudo"
fi

echo "Desinstalando cFtpfs..."
$SUDO rm -f /usr/local/bin/cftpfs
$SUDO rm -f /usr/local/share/man/man1/cftpfs.1
$SUDO mandb 2>/dev/null || true
echo "cFtpfs desinstalado correctamente."
EOFUN
chmod +x /tmp/uninstall_cftpfs.sh
$SUDO mv /tmp/uninstall_cftpfs.sh /usr/local/bin/uninstall_cftpfs.sh

print_info "¡Instalación completada!"
print_info "Uso: cftpfs ftp.example.com /mnt/ftp -u usuario -P password -f"
print_info "Ayuda: man cftpfs"
print_info "Desinstalar: sudo /usr/local/bin/uninstall_cftpfs.sh"
