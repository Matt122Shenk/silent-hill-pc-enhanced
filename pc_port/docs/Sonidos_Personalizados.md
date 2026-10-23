# Sonidos personalizados de disparos y avisos

Silent Hill PC Enhanced permite sustituir con tus propios ficheros `.wav` el
disparo de cada arma y los sonidos del aviso de objetos (toast) y de la
curación rápida. De serie el juego usa sus sonidos originales; no se
reemplaza nada hasta que eliges un fichero.

*English version: [Custom_Sounds_EN.md](Custom_Sounds_EN.md).*

---

## 1. Prepara tus ficheros de sonido

Cada sonido tiene que ser un **fichero WAV de 16 bits PCM, mono o estéreo**.
Sirve cualquier frecuencia de muestreo.

Si tu fichero está en otro formato (MP3, OGG, WAV de 24 bits o de 32 bits en
coma flotante…), conviértelo antes. Por ejemplo, con
[Audacity](https://www.audacityteam.org/):

1. Abre el fichero.
2. **Archivo → Exportar audio…**
3. Formato: **WAV (Microsoft)**; codificación: **PCM de 16 bits con signo**.
4. Guarda.

Consejos:

- Recorta el silencio del principio para que el disparo suene en el instante
  en que disparas.
- Los disparos, mejor cortos (menos de 1–2 segundos).
- Usa sonidos que tengas derecho a usar. Este proyecto no incluye ninguno.

## 2. Abre las herramientas de sonido

1. Ejecuta `SilentHillPC_Launcher.exe`.
2. Pulsa el botón **Mod Manager** de la ventana principal.
3. En el Mod Manager, pulsa **Audio**.
4. En la ventana de Audio, abre el menú **File**. Ahí están:
   - **Gunshot sounds…**: disparos (apartado 3).
   - **Interface sounds…**: avisos (apartado 4).

## 3. Disparos de las armas

La ventana **Gunshot Sounds** tiene una fila por arma: **Handgun** (pistola),
**Rifle**, **Shotgun** (escopeta) y **Blaster** (Hyper Blaster).

1. Mira la línea de abajo, **Loose files**. Si pone
   `OFF (swaps will not load)`, pulsa **Enable**. La configuración que trae
   Silent Hill PC Enhanced ya lo tiene activado.
2. En la fila del arma, pulsa **Browse…** y elige tu `.wav`.
3. Comprueba que esa fila tiene marcado **Enabled**.
4. El sonido nuevo suena **la próxima vez que se cargue el banco de sonidos
   del arma**: cambia a otra arma y vuelve, o cambia de mapa. No hace falta
   reiniciar.

Para volver al sonido original, pulsa **×** en esa fila. Para conservar tu
fichero pero usar de momento el sonido original, desmarca **Enabled**.

Dónde se guardan los ficheros (`gamedata/load/SND/` dentro de la carpeta del
juego):

| Arma | Fichero | Clave de config |
|---|---|---|
| Pistola | `PISTOL.001.wav` | `sfx_handgun` |
| Rifle | `RIFEL.004.wav` | `sfx_rifle` |
| Escopeta | `SHOTGUN.001.wav` | `sfx_shotgun` |
| Hyper Blaster | `HYPER.002.wav` | `sfx_blaster` |

`RIFEL` no es una errata: es el nombre que usa el juego original para el banco
de sonidos del rifle.

## 4. Sonidos del aviso de objetos y de la curación rápida

La ventana **Interface Sounds** tiene dos filas:

- **Item toast**: el aviso rápido de "Obtenido" / "Usado".
- **Quick heal**: el sonido al curarte con el botón de curación rápida.

1. En la fila, pulsa **Browse…** y elige tu `.wav`.
2. Comprueba que está marcado **Enabled**.
3. **Reinicia el juego** si estaba abierto: estos sonidos se cargan una vez
   por sesión, la primera vez que suenan.

Para volver al sonido por defecto (el de confirmar del juego para el aviso y
el de curación para la curación rápida), pulsa **×** o desmarca **Enabled**.

Dónde se guardan los ficheros (`gamedata/sound/custom/` dentro de la carpeta
del juego):

| Sonido | Fichero | Clave de config |
|---|---|---|
| Aviso de objetos | `toast_cue.wav` | `toast_cue` |
| Curación rápida | `heal_cue.wav` | `heal_cue` |

El aviso al recoger munición y curas necesita la **recogida silenciosa**
(`silent_take = 1`), que viene activada en la configuración incluida.

## 5. Sin el launcher (método manual)

1. Convierte tu fichero como en el apartado 1.
2. Cópialo en la carpeta indicada en las tablas, con **exactamente** ese
   nombre de fichero.
3. En `config.cfg` (junto a `SilentHillPC.exe`), comprueba que están así:
   - disparos: `allow_loose_files = 1` y la clave del arma, por ejemplo
     `sfx_handgun = 1`;
   - avisos: `toast_cue = 1` y/o `heal_cue = 1`.
4. Arranca el juego (o vuelve a equipar el arma, en el caso de los disparos).

## 6. Solución de problemas

| Problema | Solución |
|---|---|
| "That file cannot be used" | El fichero no es WAV PCM de 16 bits o tiene más de 2 canales. Conviértelo (apartado 1). |
| El disparo no cambia | Comprueba que **Loose files** está en ON y **Enabled** marcado; después cambia de arma o de mapa. |
| El sonido del aviso o de la curación no cambia | Comprueba que **Enabled** está marcado y reinicia el juego. |
| No aparece el aviso al recoger munición | Activa la recogida silenciosa (`silent_take = 1`) en `config.cfg`. |
| Quiero recuperar los sonidos originales | Pulsa **×** en la fila, o borra el fichero de las carpetas indicadas. |
