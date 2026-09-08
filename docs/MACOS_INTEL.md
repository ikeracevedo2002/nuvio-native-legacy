# macOS Intel (x86_64)

Esta variante ejecuta el binario de forma nativa en un Mac Intel. La
reproduccion usa `libmpv` embebido y su Render API OpenGL dentro de la misma
ventana SDL2; no lanza `mpv`, no usa `system()`/`popen()` y no crea una segunda
ventana.

## Dependencias de desarrollo

Se necesitan Xcode Command Line Tools, `pkg-config`, SDL2, SDL2_image,
SDL2_ttf y una construccion de `libmpv` que exporte headers y un archivo
`.pc`. MacPorts es la ruta recomendada para mantener un entorno Intel; también
puede usarse otro prefijo de desarrollo. Ninguna ruta de Homebrew/MacPorts se
graba en el bundle final.

```sh
bash tools/bootstrap-macos-intel.sh
```

El script solo comprueba el entorno; no instala paquetes ni modifica el
sistema.

## Compilar y ejecutar

```sh
bash tools/mac-intel.sh --no-run
bash tools/mac-intel.sh
```

El compilador recibe siempre `-arch x86_64`, sin `-march=native` ni
`-mcpu=native`. El binario resultante aparece en
`build/macos-x86_64/Debug/Nuvio` y se comprueba con `file` y `otool -L`.

La ventana es redimensionable, conserva el área lógica 16:9, deja el cursor
visible y limita el primer plano a aproximadamente 60 fps. Sin foco baja a
aproximadamente 15 fps. `NUVIO_MAC_VSYNC=1` permite probar vsync del driver;
el valor por defecto es el limiter manual.

Argumento de arte opcional:

```sh
bash tools/mac-intel.sh --no-run -- /ruta/a/art
```

En un `.app`, la arte se resuelve desde
`Contents/Resources/app/art`. En desarrollo se acepta `deploy/app/art`.
Los datos persistentes se guardan en `~/Library/Application Support/Nuvio`,
con una migración conservadora de archivos de `~/.nuvio` cuando el destino
está vacío. El caché queda bajo `~/Library/Caches/Nuvio` cuando se configura
desde el módulo de datos.

## Smoke de libmpv

El smoke crea una ventana OpenGL 2.1, obtiene los símbolos GL mediante
`SDL_GL_GetProcAddress`, carga un archivo local y comprueba eventos de archivo,
duración, audio activo, pausa, seek y reanudación:

```sh
bash tools/macos-libmpv-smoke.sh /ruta/a/video.mp4
# o
bash tools/mac-intel.sh --no-run --smoke --video /ruta/a/video.mp4
```

El archivo debe tener una pista de video y una pista de audio. El smoke es una
prueba real de runtime y no se marca como pasado por una comprobación estática.

## Bundle y dylibs

```sh
bash tools/package-macos-intel.sh --no-sign
bash tools/validate-macos-bundle.sh build/Nuvio.app
```

El empaquetador copia `Contents/MacOS/Nuvio`, `Resources/app/art`, fuentes y
las dylibs no pertenecientes al sistema que encuentra mediante `otool -L`.
Reescribe sus IDs y referencias a `@rpath`, añade
`@executable_path/../Frameworks` y vuelve a recorrer las dependencias. No se
copian `/System/Library`, `/usr/lib` ni archivos de usuario.

Por defecto se aplica firma ad hoc (`codesign --sign -`). Para una firma de
distribución:

```sh
MACOS_SIGN_IDENTITY="Developer ID Application: ..." \
  bash tools/package-macos-intel.sh
```

`tools/sign-macos.sh` acepta `MACOS_ENTITLEMENTS` para un archivo de
entitlements y firma primero las dylibs, después el ejecutable y finalmente el
bundle.

## Controles y límites conocidos

- `Esc`, Backspace y Back normalizan a volver; `Space` controla play/pause.
- `Cmd+Q` cierra el proceso; `Cmd+Ctrl+F` alterna fullscreen.
- El render OpenGL de macOS es SDR. No se muestra una insignia Dolby Vision
  basándose únicamente en metadata de la fuente.
- Subtítulos embebidos y externos usan las propiedades de libmpv. La selección
  de audio/subtítulos conserva el ID de pista de mpv y no confunde ID con
  índice de UI.
- Los créditos Matroska específicos del backend webOS no se inventan en macOS;
  `video_creditos()` devuelve cero si el archivo no expone esa información a
  través de libmpv.

## Validación honesta

La validación completa requiere un Mac Intel con SDL2/libmpv, un archivo local
y una pantalla. En otros hosts se pueden ejecutar las comprobaciones de shell
y revisar el diff, pero no se debe declarar pasado el smoke ni la reproducción
real.
