# cFtpfs - Sistema de archivos FTP en C

Implementación en C del sistema de archivos FUSE para montar servidores FTP, basado en PyFtpfs.

## Características

- **Monta servidores FTP como sistema de archivos local** usando FUSE
- **Soporte para lectura y escritura** de archivos
- **Cache de directorios** con timeout configurable (5 segundos)
- **Sistema de escritura temporal** para compatibilidad con VS Code
- **Soporte para formatos de listado FTP** Unix y Windows
- **Conexiones thread-safe** con manejo de locks
- **Múltiples opciones de conexión** (puerto, usuario, contraseña, codificación)
- **Operaciones completas**: crear, leer, escribir, eliminar, renombrar, directorios

## Dependencias

### Ubuntu/Debian
```bash
sudo apt-get install libfuse3-dev libcurl4-openssl-dev build-essential
```

### Fedora/RHEL
```bash
sudo dnf install fuse3-devel libcurl-devel gcc make
```

### Arch Linux
```bash
sudo pacman -S fuse3 curl gcc make
```

## Instalación

### Método 1: Script de instalación automático (Recomendado)
```bash
chmod +x install.sh
sudo ./install.sh
```

El script detectará automáticamente tu sistema operativo e instalará todas las dependencias necesarias.

### Método 2: Compilación manual
```bash
make
sudo make install
```

### Desinstalación
```bash
sudo make uninstall
# o
sudo /usr/local/bin/uninstall_cftpfs.sh
```

## Uso

### Sintaxis básica

```bash
cftpfs <host> <mountpoint> [opciones]
```

### Opciones

| Opción | Descripción | Default |
|--------|-------------|---------|
| `-p, --port=PUERTO` | Puerto FTP | 21 |
| `-u, --user=USUARIO` | Usuario FTP | anonymous |
| `-P, --password=PASS` | Contraseña FTP | (vacío) |
| `-e, --encoding=ENC` | Codificación | utf-8 |
| `-d, --debug` | Modo debug con logs detallados | - |
| `-f, --foreground` | Ejecutar en primer plano | - |
| `-h, --help` | Mostrar ayuda | - |

### Ejemplos

Montar en primer plano con usuario y contraseña:
```bash
cftpfs ftp.example.com /mnt/ftp -u miusuario -P mipassword -f
```

Montar en background:
```bash
cftpfs ftp.example.com /mnt/ftp -u usuario -P password
```

Montar servidor FTP anónimo:
```bash
cftpfs ftp.gnu.org /mnt/gnu -f
```

### Desmontar

```bash
fusermount -u /mnt/ftp
```

## Arquitectura

El proyecto está organizado en los siguientes módulos:

```
cFtpfs/
├── include/
│   └── cftpfs.h          # Definiciones y estructuras de datos
├── src/
│   ├── main.c            # Punto de entrada y operaciones FUSE
│   ├── ftp_client.c      # Cliente FTP usando libcurl
│   ├── ftp_client_mock.c # Versión mock para testing
│   ├── cache.c           # Sistema de cache de directorios
│   ├── handles.c         # Gestión de handles de archivos
│   └── parser.c          # Parser de listados FTP (Unix/Windows)
├── Makefile              # Script de compilación
├── install.sh            # Script de instalación automática
└── README.md             # Documentación
```

## Operaciones soportadas

- **Navegación**: `getattr`, `readdir`
- **Lectura**: `open`, `read`
- **Escritura**: `create`, `write`, `truncate`
- **Gestión**: `unlink`, `mkdir`, `rmdir`, `rename`
- **Metadatos**: `chmod`, `chown`, `utimens` (stubs - no soportados por FTP)

## Sistema de Cache

- **Timeout**: 5 segundos para listados de directorios
- **Estrategia**: Copy-on-read para evitar race conditions
- **Invalidación**: Automática en operaciones de escritura

## Limitaciones

- No soporta cambio de permisos reales (chmod) - FTP estándar no lo permite
- No soporta cambio de propietario (chown)
- No soporta cambio de timestamps (utimens)
- Los symlinks son detectados pero no seguidos
- Timeout de cache fijo en 5 segundos

## Modos de Compilación

### Versión Mock (desarrollo/testing)
```bash
make
```
Usa un cliente FTP simulado que retorna datos de prueba. Útil para desarrollo y testing sin necesidad de un servidor FTP real.

### Versión Real (producción)
```bash
make real
```
Requiere libcurl instalado. Usa conexiones FTP reales.

## Rendimiento

- **Lectura**: Similar a cliente FTP estándar
- **Escritura**: Optimizada para editores (VS Code) con archivos temporales
- **Cache**: Reduce operaciones de red para listados de directorios

## Troubleshooting

### Error: "fuse: device not found"
Asegúrate de que el módulo FUSE está cargado:
```bash
sudo modprobe fuse
```

### Error: "Permission denied"
Asegúrate de que tu usuario está en el grupo `fuse`:
```bash
sudo usermod -a -G fuse $USER
# Cerrar sesión y volver a iniciar para aplicar cambios
```

### Error de conexión FTP
Verifica que el servidor FTP permite conexiones pasivas y que el puerto está abierto:
```bash
telnet ftp.example.com 21
```

### Error "munmap_chunk(): invalid pointer"
Este error fue corregido en la versión actual. Si persiste, asegúrate de usar la última versión del código.

## Diferencias con PyFtpfs

- **Rendimiento**: Implementación nativa en C es más rápida
- **Memoria**: Menor consumo de memoria
- **Dependencias**: Usa libcurl en lugar de ftplib (Python)
- **Threading**: Implementación manual con pthreads

## Licencia

Apache 2.0 - Igual que el proyecto original PyFtpfs

## Autor

Implementación en C basada en PyFtpfs

## Contribuir

Las contribuciones son bienvenidas. Por favor:

1. Fork el repositorio
2. Crea una rama para tu feature (`git checkout -b feature/nueva-funcionalidad`)
3. Commit tus cambios (`git commit -am 'Agregar nueva funcionalidad'`)
4. Push a la rama (`git push origin feature/nueva-funcionalidad`)
5. Abre un Pull Request

## Historial de Versiones

- **v1.0.0** - Versión inicial completa
  - Soporte completo de operaciones FUSE
  - Sistema de cache
  - Gestión de archivos temporales
  - Parser de listados Unix/Windows
  - Instalador automático
  - Corrección de errores de memoria

## Agradecimientos

Este proyecto está basado en PyFtpfs y utiliza:
- FUSE3 para el sistema de archivos en espacio de usuario
- libcurl para conexiones FTP
- pthreads para threading

## Contacto

Para reportar bugs o solicitar funcionalidades, por favor abre un issue en el repositorio.# cFtpFs
