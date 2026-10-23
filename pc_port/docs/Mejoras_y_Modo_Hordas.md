# Silent Hill PC Port — Mejoras añadidas y Modo Hordas

Este documento recoge las mejoras añadidas sobre el port original
**beta-2026.09.04.1**. Lo que ya traía esa versión (cámaras alternativas,
minimapa, mirilla, decals de bala, randomizer, texturas HD, etc.) no se
describe aquí; para eso está el `CHANGELOG.md`.

Todo es opcional. El `config.cfg` que trae esta versión viene ajustado para la
mejor experiencia (sobre todo con mando), con muchas de estas opciones ya
activadas; si borras una clave, vuelve a su valor original, casi siempre
desactivado. Se ajusta desde el menú rápido **F10** y se guarda solo en
`config.cfg`. Entre paréntesis va la clave de configuración de cada opción.

*English version: [Enhancements_and_Horde_Mode_EN.md](Enhancements_and_Horde_Mode_EN.md).*

---

## Índice

1. [Modo Hordas](#1-modo-hordas)
2. [Combate y controles](#2-combate-y-controles)
3. [HUD y avisos](#3-hud-y-avisos)
4. [Feedback visual](#4-feedback-visual)
5. [Inmersión](#5-inmersión)
6. [Dificultad independiente](#6-dificultad-independiente)
7. [Menú F10: páginas nuevas](#7-menú-f10-páginas-nuevas)
8. [Menú de pausa navegable](#8-menú-de-pausa-navegable)
9. [Audio y launcher](#9-audio-y-launcher)
10. [Integraciones](#10-integraciones)
11. [Cómo funciona el Modo Hordas](#11-cómo-funciona-el-modo-hordas)

---

## 1. Modo Hordas

Un modo extra de supervivencia por oleadas, con un toque roguelike, separado
de la campaña. Se elige desde el menú principal (**HORDE / MODO HORDAS**). El
funcionamiento completo está en la [sección 11](#11-cómo-funciona-el-modo-hordas).

## 2. Combate y controles

- **Aim Camera** (`aim_camera_mode`): usa otra cámara solo mientras apuntas
  (Auto / Classic / TPS / OTS / FPS). Por ejemplo, explorar con la cámara
  clásica y apuntar en primera persona.
- **Melee Aim Walk** (`melee_aim_walk`): al apuntar con un arma cuerpo a cuerpo
  en TPS/OTS, Harry camina despacio, como en las cámaras fijas, en lugar de
  soltar el arma al correr.
- **D-Pad Weapons** (`dpad_weapons`, `dpad_up/down/left/right`): la cruceta
  del mando elige armas en vez de mover a Harry. Cada dirección tiene un arma
  asignada; se cambia desde F10 o desde el propio inventario. Si asignas un
  arma que ya está en otra dirección, se intercambian.
- **Auto Reload** (`auto_reload`): recarga automática, con animación, al
  vaciar el cargador si queda munición de reserva.
- **Manual Reload** (`manual_reload`): opción para activar o desactivar la
  recarga con R / Cuadrado mientras apuntas.
- **Quick Heal (mejorado)** (`quick_heal`): interruptor general de la
  curación rápida (L3). Ahora muestra un aviso con la cura usada, tiene un
  destello verde de intensidad ajustable y admite un sonido propio (ver
  [sección 9](#9-audio-y-launcher)).

## 3. HUD y avisos

- **HUD de munición** (`ammo_hud`, `ammo_always`, `ammo_corner`, `ammo_scale`,
  `ammo_opacity`): contador de cargador y reserva para armas de fuego. Se ve
  al apuntar, o siempre si tienes un arma de fuego equipada. Esquina, tamaño y
  opacidad configurables.
- **HUD de vida de Harry** (`hp_hud`, `hp_style`, `hp_number`, `hp_corner`,
  `hp_scale`, `hp_opacity`, `hp_always`): barra clásica o línea de pulso
  cardiaco animada, que se acelera con poca vida. Cambia de color de verde a
  rojo y puede mostrar el número de vida. Siempre visible, o solo unos segundos
  tras recibir daño o curarte.
- **Barras de vida de enemigos** (`enemy_hp_bars`): barras rojas sobre los
  enemigos vivos que estén a la vista y a menos de 12 m. No se ven a través de
  las paredes.
- **Avisos (toast) de objetos**: aviso rápido de "Obtenido" / "Usado" para:
  - munición y curas recogidas sin pasar por la pantalla de recogida
    (`silent_take`);
  - objetos dados por la consola (`GIVE`), el randomizer, el trainer o el Modo
    Hordas;
  - curas usadas con la curación rápida.

  Opciones: esquina, tamaño, opacidad y fondo (`item_toast_corner`,
  `item_toast_scale`, `item_toast_opacity`, `item_toast_bg`).
  - **Nombres traducidos:** tabla propia en inglés y en español para armas,
    munición y curas (por ejemplo "First Aid Kit" / "Botiquín"). El resto de
    objetos usa el nombre del juego, corrigiendo las tildes del disco PAL, que
    antes se veían rotas ("Malet…" en vez de "Maletín").
  - **Idioma:** "Obtained" / "Used" en inglés; "Obtenido" / "Usado" si el
    idioma del juego es el español.

## 4. Feedback visual

Todos estos destellos pasan por el post-procesado, así que se combinan con
CRT, scanlines y los demás filtros sin pisarse.

- **Sacudidas de cámara**:
  - al recibir daño (`shake_hurt`);
  - al disparar (`shake_fire`), en el momento real en que sale la bala;
  - al dañar a un enemigo (`shake_dealt`).

  Cada una tiene su intensidad, de 0 a 200 % (`shake_*_int`).
- **Hitmarker** (`hitmarker`): la mirilla parpadea en rojo al matar.
- **Kill Flash** (`kill_flash`, `kill_flash_mode`): destello breve a pantalla
  completa al matar, en blanco y negro o con viñeta negra.
- **Heal Flash** (`heal_flash`): intensidad del destello verde al curarse
  (0 lo desactiva).
- **Low Health Glow** (`low_health_glow`, `low_health_op`): resplandor rojo
  pulsante en los bordes con poca vida.
- **Hurt Flash** (`hurt_flash`, `hurt_flash_op`): destello rojo oscuro al
  recibir daño.

## 5. Inmersión

- **Weapon Immersion** (`immersion_weapons`): el arma equipada se oculta a los
  pocos segundos de no usarla. Reaparece al equipar, apuntar, disparar,
  recargar o cuando la radio detecta peligro. Recoger objetos ya no la vuelve
  a mostrar.
- **Fear Panting** (`immersion_fear`): Harry jadea de miedo de vez en cuando
  mientras suena la radio.
- **Flicker** (`immersion_flicker`): la linterna parpadea en ráfagas cortas
  cerca de enemigos vivos (15 % si están cerca, 35 % si los miras). Solo en los
  modos de linterna por píxel, no en Classic.
- **Blood Walls**: salpicaduras de sangre en la pared detrás de los enemigos
  heridos por bala.

## 6. Dificultad independiente

Se aplica encima de la dificultad del juego, que no se toca. En **Normal**
el juego es idéntico al original.

| Opción | Valores |
|---|---|
| **Munición** (`diff_ammo`) | Low 15 % · Medium 50 % · Normal 100 % · High 170 % de la munición recogida |
| **Curas** (`diff_heals`) | Low 25 % · Medium 50 % · Normal 100 % · High 150 % de lo que cura cada objeto |
| **Daño enemigo** (`diff_enemy`) | Low 50 % · Medium 75 % · Normal 100 % · High 250 % · Very High 400 % · Hell 700 % |

## 7. Menú F10: páginas nuevas

Además de las opciones de arriba, el F10 tiene estas páginas nuevas:
**HUD 2**, **Immersion**, **Difficulty**, **Health**, **Visual Feedback** y
**Trainer**.

- **Trainer**: da un arma de fuego elegida con 30 balas de su munición, un
  arma cuerpo a cuerpo, o 3 unidades de la cura elegida. No hace falta usar la
  consola.
- Cada fila tiene su descripción en `Quick_Options_Reference.md` (español) y
  `Quick_Options_Reference_EN.md` (inglés).

## 8. Menú de pausa navegable

Antes la pausa solo mostraba el texto "PAUSA". Ahora es un menú sobre la
imagen congelada de la partida, con estas opciones:

**Reanudar · Cargar · Opciones rápidas · Menú principal · Salir**

Se ve en inglés por defecto y en español si el idioma del juego es el español.

## 9. Audio y launcher

- **Disparos personalizados por arma** (launcher → Audio tool → *SFX swap*):
  un `.wav` por arma (pistola, rifle, escopeta, Hyper Blaster), copiado al
  sitio correcto en `gamedata/load/SND/`. Cada arma tiene su interruptor
  (`sfx_handgun`, `sfx_rifle`, `sfx_shotgun`, `sfx_blaster`). El cambio se
  aplica al recargar el banco de sonidos (al equipar o cambiar de mapa), sin
  reiniciar.
- **Sonidos de interfaz** (launcher → Audio tool → *UI sounds*): sonidos
  propios para el aviso de objeto (`toast_cue`) y para la curación rápida
  (`heal_cue`), guardados en `gamedata/sound/custom/`. Sin fichero, o con la
  opción apagada, se usan los sonidos de siempre.
- Ajustes en la ventana principal del launcher, el Mod Manager y los
  conversores para dar cabida a estas herramientas.
- **Actualizaciones desactivadas**: al ser un fork no oficial, el launcher no
  busca ni descarga builds del proyecto original (que borrarían estos
  cambios). Los enlaces al proyecto original, su web y su Discord se
  mantienen; "Report Bug" lleva a las incidencias de este fork.
- **Discord Rich Presence desactivado por defecto**, porque la app de Discord
  integrada pertenece al proyecto oficial. Se puede activar en `config.cfg`.

## 10. Integraciones

- **Consola y randomizer**: los objetos dados con `GIVE` y los regalos del
  randomizer muestran el aviso de objeto.

---

## 11. Cómo funciona el Modo Hordas

### Entrada y aislamiento de las partidas

- Se elige **HORDE / MODO HORDAS** en el menú principal y después la
  dificultad, como en una partida nueva.
- La partida de hordas es **desechable**: arranca desde una partida nueva en
  memoria y nunca guarda. No hay autoguardado, la escritura en la memory card
  está bloqueada y "Continuar" termina la partida de hordas antes de cargar la
  tuya. **Tus partidas de la campaña nunca se tocan.**
- Las muertes del modo hordas tampoco cuentan para los enemigos muertos de la
  campaña.

### Arena

- Las calles del **Old Silent Hill**, a la salida de la cafetería
  (MAP2_S00). La escena de salida de la cafetería se salta.
- **Todas las puertas están cerradas** (suena el aviso de cerradura); no se
  puede salir de la arena.
- Tienes todos los mapas de papel desde el principio, y el minimapa marca los
  enemigos vivos de la oleada.
- Equipo inicial: linterna, radio, cuchillo de cocina y pistola con 15 balas.

### Oleadas

- Cuenta atrás de **3 segundos** y después aparece un grupo de **5, 7 o 9**
  enemigos, mezclando Grey Children, Puppet Nurses, Rompers y Groaners. Los
  enemigos normales de la zona están desactivados.
- Aparecen **detrás o a los lados** de Harry, a 8–14 m, nunca delante: en
  terreno al mismo nivel que él (nunca en tejados ni sótanos), con línea
  directa hasta él y en zonas abiertas (calles y callejones, no interiores).
- Un enemigo que pasa más de 4 s a más de 20 m se recoloca alrededor de Harry,
  para que la presión no baje.
- La oleada termina cuando no queda ningún enemigo vivo. Entonces se abre la
  **tienda de recompensas**.
- **Amenaza**: desde la oleada 2, la vida y el daño de los enemigos suben
  **x1.25 por oleada**, hasta un máximo de **x10** (oleada 12).

### Loot por cada muerte

| Probabilidad | Resultado |
|---|---|
| 5 % | Nada |
| 15 % | Curación: bebida (75 %) o botiquín (25 %) |
| 80 % | Munición de un arma que tengas: pistola 5–10, escopeta 2–4, rifle 2–4 |

### Tienda de recompensas (entre oleadas)

El juego se pausa y aparecen **3 cartas**; eliges una gratis y empieza la
siguiente cuenta atrás. No se puede saltar.

**Cómo se eligen las cartas:** se reúnen las recompensas posibles y se sacan
3 al azar.

| Recompensa | Cuándo puede salir | Qué da |
|---|---|---|
| **Munición** | Si tienes algún arma de fuego | 2 cargadores del arma con **menos munición** (contando cargadores, no balas sueltas): pistola +30, escopeta +12, rifle +12 |
| **Curación** | Siempre | Bebida tónica x2–3 (50 %), botiquín x1–2 (35 %) o ampolla x1 (15 %) |
| **Arma de fuego** | Si te falta alguna (rifle, escopeta) | El arma con un cargador de munición |
| **Arma cuerpo a cuerpo** | Si te falta alguna | Tubería, taladro, martillo, motosierra, katana o hacha |
| **Mejora de daño** | 50 % por tienda (45 % si ya tienes pistola, rifle y escopeta pero te falta alguna arma cuerpo a cuerpo) | x1.50, x2.50, x2.75 o x4.00, acumulables hasta x100 |
| **Vida máxima** | Misma probabilidad que el daño, en tirada aparte | +18 %, +25 % o +33 % de vida efectiva (ver abajo) |

Si hay menos de 3 recompensas posibles, se rellena con una segunda curación
distinta y, solo si aún falta, con una mejora de daño. La Hyper Blaster nunca
sale (tiene munición infinita).

**Vida máxima:** el juego original fija la vida de Harry en 100, así que
esta mejora reduce el daño que recibe: +25 % de vida equivale a recibir x0.80
del daño. Se acumula hasta x5 (los enemigos con amenaza x10 siguen haciendo el
doble de daño). Las curas, la barra de vida y el electrocardiograma funcionan
como siempre.

**Controles de la tienda:**

| Acción | Teclado | Mando | Ratón |
|---|---|---|---|
| Cambiar de carta | A / D o flechas | Cruceta o stick | Mover el puntero sobre una carta, o la rueda |
| Elegir | Enter, Espacio o C | Botón de confirmar | Clic izquierdo |

- Durante el primer medio segundo no se puede elegir. Un botón que ya estaba
  pulsado al abrirse la tienda (por ejemplo, el disparo que mató al último
  enemigo) tiene que soltarse y volver a pulsarse.
- El ratón solo cambia la selección cuando se mueve, así que no interfiere con
  el teclado ni con el mando.

### HUD del modo

- **Arriba**: oleada, enemigos vivos (o la cuenta atrás de la siguiente
  oleada), muertes y amenaza.
- **A la izquierda**: tu multiplicador de **daño** y de **vida**.
- El HUD y la tienda se ven en español si el idioma del juego es el español y
  en inglés en cualquier otro caso.

### Detalles técnicos

- El HUD y la tienda se dibujan como el menú F10: una sola textura de texto
  creada al arrancar, sin crear ni borrar texturas durante la partida, y con
  todo el estado gráfico restaurado. Así se evitan los fallos gráficos que
  causaban los primeros prototipos.
- El modo tiene su propio generador aleatorio, para no alterar el
  comportamiento de los enemigos ni las demos del juego.
- La sangre del suelo se limpia sola en la arena: el juego original solo la
  borra al cambiar de sala, y sin limpieza dejaría de aparecer sangre nueva.
