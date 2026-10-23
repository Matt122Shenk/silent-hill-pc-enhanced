# Referencia de opciones rápidas (F10) — borrador en español

> BORRADOR para revisión. Cada fila del menú F10 con su descripción en español.
> Cuando se apruebe, se creará la versión en inglés.
> Convención: "Apagado/Encendido" para Off/On. Los valores entre paréntesis son
> los que muestra el menú.

Abrir con F10 (reconfigurable). Navegación: flechas o cruceta para moverse,
izquierda/derecha para cambiar valores, confirmar con Enter, cancelar con
Escape, Q/E o AvPág/RePág para cambiar de página, ratón compatible.
Cada página termina en "Prev page" (anterior), "Next page" (siguiente) y
"Close" (cerrar). Los cambios se guardan automáticamente en `config.cfg`.

## GRAPHICS

- **Texture Filter** (Off / Dither / Bilinear / Trilinear / Aniso 2x–16x):
  Filtrado de texturas. Dither es el aspecto fiel a PSX.
- **Antialiasing** (Off / 2x / 4x / 8x): Suavizado de bordes. Requiere
  reiniciar el juego (marcado con *).
- **Post Process** (Off / CRT / Scanlines / Vignette / Color Grade /
  Film Grain / Sharpen / PSX Retro / Cinematic): Filtro de imagen a pantalla
  completa.
- **Tone Mapping** (Off / Reinhard / ACES / Filmic): Mapeo de tonos de la
  imagen final.
- **Fog Strength** (0–2): Densidad de la niebla. 1.1 equivale a la PSX;
  bajarlo permite ver más lejos.
- **Flashlight Mode** (Classic / Classic + Shadows / Modern / Modern +
  Shadows): Classic es la linterna original por vértices; Modern es un foco
  estilizado por píxel, con o sin sombras en tiempo real.
- **Flashlight Intensity** (0–3): Brillo del cono de la linterna.
- **Flashlight Size** (0–3): Cobertura del cono de la linterna.
- **Shadow Resolution** (256–8192): Resolución del mapa de sombras de la
  linterna. Más alto = sombras más nítidas y más costoso para el rendimiento.
- **Bullet Decals** (Apagado/Encendido): Marcas de bala donde los disparos del
  jugador impactan (hasta 64, se borran al cambiar
  de mapa).
- **Enemy HP Bars** (Apagado/Encendido): Barras rojas de vida sobre enemigos
  vivos a la vista y a menos de 12 m. No se ven a través de paredes.

## HUD & AUDIO

- **Minimap** (Off / Square / Circle): Minimapa en pantalla con estilos cuadrado y circular.
- **Minimap Scale** (60–120 %): Tamaño del minimapa.
- **Minimap Corner** (esquinas): Esquina del minimapa.
- **Minimap Opacity** (0–100 %): Opacidad del minimapa.
- **Minimap Require Map** (Apagado/Encendido): Solo dibuja el minimapa si
  Harry ya encontró el mapa de papel de la zona.
- **Crosshair** (Apagado/Encendido): Mirilla al apuntar en TPS/OTS; siempre
  visible en primera persona.
- **Toast Corner / Scale / Opacity**: Esquina, tamaño (60–130 %) y opacidad
  (20–100 %) de los avisos de objetos recogidos o curas utilizadas. El sonido de la notificación puede personalizarse desde el launcher.
- **Speaker Layout** (Auto / Stereo / Quad / 5.1 / 7.1): Configuración de
  altavoces.
- **Music Volume** (0–16): Volumen de la música.
- **Effects Volume** (0–16): Volumen de los efectos.
- **FMV Volume** (0–1): Volumen de las cinemáticas de vídeo.

## HUD 2

- **Ammo Corner / Scale / Opacity**: Esquina, tamaño (60–130 %) y opacidad
  (20–100 %) del contador de munición.
- **Ammo Always** (Apagado/Encendido): El contador queda visible con arma de
  fuego equipada; si no, solo al apuntar.
- **Ammo HUD** (Apagado/Encendido): Interruptor general del contador.
- **Auto Reload** (Apagado/Encendido): Recarga automática con animación al
  vaciar el cargador habiendo reserva.
- **Manual Reload** (Apagado/Encendido): La tecla R / botón Cuadrado/X recarga el
  arma apuntando si admite recarga.
- **D-Pad Weapons** (Apagado/Encendido): La cruceta del mando elige armas en
  vez de mover. También se pueden asignar desde el inventario.
- **D-Pad Up / Down / Left / Right** (arma o None): Arma asignada a cada
  dirección. Asignar un arma ocupada en otra dirección las intercambia.
- **Quick Heal** (Apagado/Encendido): Curación rápida con el mejor objeto
  disponible. Utiliza L3.
- **Toast Background** (0–100 %): Opacidad del fondo de los avisos.

## VIEW & ASPECT

- **Control Type** (Simple / Advanced): Controles de forma de imagen. Simple
  usa Aspect Trim; Advanced usa Horizontal/Vertical FOV y Pixel Aspect.
- **Aim Camera** (Auto / Classic / TPS / OTS / FPS): Permite utilizar una cámara diferente al apuntar.
- **Melee Aim Walk** (Apagado/Encendido): Apuntar con melee en TPS/OTS fuerza
  paso lento (como en cámaras fijas) en vez de soltar el arma al correr.
- **Aspect Trim** (0.50–1.50, Simple): Ajuste fino del aspecto 4:3.
- **Horizontal / Vertical FOV** (0.25–2.00, Advanced): Campo de visión.
- **Vertical Shift** (±60 filas): Desplazamiento vertical de la vista en
  juego.
- **Cutscene Shift** (±60 filas): Desplazamiento vertical en cinemáticas.
- **Pixel Aspect** (0.50–2.00, Advanced): Aspecto de píxel.
- **Reset View Settings**: Restaura todos los valores de esta página.

## CHEATS

- **God mode** (Apagado/Encendido): Invencibilidad (igual que la tecla 7).
- **Noclip** (Apagado/Encendido): Atravesar paredes.
- **Enemies ignore Harry** (Apagado/Encendido): Los enemigos ignoran a Harry
  (igual que la tecla 9).
- **Unlimited enemies** (Apagado/Encendido): Eleva el límite de enemigos
  simultáneos por sala.
- **Handgun bullets +15**: Otorga 15 balas de pistola (acción inmediata).
- **Hunting rifle +30**: Otorga el rifle si falta + 30 cartuchos (acción).
- **Shotgun +30**: Otorga la escopeta si falta + 30 cartuchos (acción).
- **Kill nearby enemies** (acción): Mata a los enemigos cercanos por su vía
  normal de muerte.
- **Play as** (lista): Jugar con otro personaje (conserva animaciones de
  Harry). Navegable como desplegable.
- **Free camera** (Apagado/Encendido): Cámara libre de depuración.

## DEBUG

- **Allow console** (Apagado/Encendido): Activa la consola (`~`) y teclas de
  desarrollo.
- **Collision visualizer** (Apagado/Encendido): Panel y líneas de la colisión
  de Harry.
- **Keyframe viewer (K)** (Apagado/Encendido): Inspector de keyframes de
  animación (K, `,`/`.`).
- **Fast forward (Ctrl+F5)** (Apagado/Encendido): Avance rápido conmutable
  (la tecla es pulsada; se apaga solo si Harry muere).
- **Wireframe (Ctrl+F1)** (Apagado/Encendido): Render en alambre.
- **No textures (Ctrl+F2)** (Apagado/Encendido): Render sin texturas.
- **Spawn** (lista): Genera un monstruo de la lista al confirmar.
- **Log Harry position** (acción): Escribe posición y mapa en el log.
- **Log camera shot** (acción): Escribe el plano de cámara en el log.
- **Kill Harry** (acción): Mata a Harry al instante.

## TRAINER

- **Weapon** (lista): Arma a otorgar (pistola, rifle, escopeta, bláster).
- **Give Weapon + Ammo** (acción): Otorga el arma de fuego elegida (+30 de su
  munición).
- **Melee** (lista): Arma cuerpo a cuerpo a otorgar (cuchillo, tubería,
  taladro, martillo, sierra, katana, hacha).
- **Give Melee** (acción): Otorga el arma elegida.
- **Heal** (lista): Curación a otorgar (bebida, botiquín, ampolla).
- **Give Heals x3** (acción): Otorga 3 unidades de la curación elegida.

## IMMERSION

- **Weapon Immersion** (Apagado/Encendido): El arma equipada se oculta a los
  segundos y reaparece al equipar, apuntar, disparar, recargar o si Harry detecta peligro (cuando la radio emite sonidos).
- **Fear Panting** (Apagado/Encendido): Harry jadea de miedo al azar mientras
  la radio suena.
- **Blood Walls** (Apagado/Encendido): Salpicaduras de sangre en la pared tras
  enemigos heridos por bala.
- **Flicker** (Apagado/Encendido): Cerca de enemigos vivos la linterna falla,
  se apaga medio segundo y no se puede volver a encender hasta que se
  recupera (15 % cerca, 45 % mirándolo; solo modos por píxel, no Classic).

## DIFFICULTY (independiente de la dificultad nativa; Normal = vanilla)

- **Ammo** (Low / Medium / Normal / High): Munición del loot al 15 / 50 /
  100 / 170 %. P. ej. 15 balas pasan a 2 / 8 / 15 / 26.
- **Heals** (Low / Medium / Normal / High): Potencia o disminuye el efecto de curas al 25 / 50 /
  100 / 150 %.
- **Enemy Damage** (Low / Medium / Normal / High / Very High / Hell): Daño
  enemigo aumenta o baja al 50 / 75 / 100 / 250 / 400 / 700 %.

## HEALTH

- **Health HUD** (Apagado/Encendido): Interruptor general del indicador de
  vida de Harry.
- **Style** (Bar / Pulse): Barra clásica o línea de pulso cardiaco animado
  (acelera con vida baja).
- **Number** (Apagado/Encendido): Número de vida a la derecha.
- **Corner** (esquinas): Esquina extrema del indicador.
- **Scale** (60–130 %): Tamaño.
- **Opacity** (20–100 %): Opacidad.
- **Always Show** (Apagado/Encendido): Siempre visible; si no, solo unos
  segundos tras recibir daño o curarse.

## VISUAL FEEDBACK

- **Hurt Kick** (Apagado/Encendido): Sacudida de cámara al recibir daño.
- **Hurt Strength** (0–200 %): Intensidad de la anterior.
- **Fire Kick** (Apagado/Encendido): Sacudida al disparar (al salir la bala,
  igual en las 4 armas).
- **Fire Strength** (0–200 %): Intensidad de la anterior.
- **Dealt Kick** (Apagado/Encendido): Sacudida al dañar a un enemigo.
- **Dealt Strength** (0–200 %): Intensidad de la anterior.
- **Hitmarker** (Apagado/Encendido): La mirilla parpadea en rojo al matar
  (requiere la mirilla activa).
- **Kill Flash** (Apagado/Encendido): Destello fugaz a pantalla completa al
  matar.
- **Flash Style** (B&W / Vignette): Estilo del kill flash. Blanco y negro o viñeta negra.
- **Heal Flash** (0–100 %): Intensidad del destello verde al curarse (0 lo
  desactiva).
- **Low Health Glow** (Apagado/Encendido): Resplandor rojo pulsante en los
  bordes con vida baja.
- **Low Opacity** (0–100 %): Opacidad del resplandor con vida baja.
- **Hurt Flash** (Apagado/Encendido): Destello rojo oscuro a pantalla completa
  al recibir daño.
- **Hurt Opacity** (0–100 %): Opacidad del destello al recibir daño.
