Ghid complet configurare Blynk
Template: TMPL42ximIY6M — „Add agency"
1. DATASTREAMS
Mergi la Blynk Console → Templates → TMPL42ximIY6M → Datastreams.

Pin	Nume	Tip	Min	Max	Default	Unitate	Note
V1	Temp Stânga	Double	-10	80	0	°C	Read-only
V2	Umiditate Stânga	Double	0	100	0	%	Read-only
V3	Temp Dreapta	Double	-10	80	0	°C	Read-only
V4	Umiditate Dreapta	Double	0	100	0	%	Read-only
V5	Vent Stânga	Integer	0	1	0	—	Bidirecțional RW
V6	Vent Dreapta	Integer	0	1	0	—	Bidirecțional RW
V7	Prag Temperatură	Double	20	80	45	°C	RW
V8	Prag Umiditate	Double	20	100	60	%	RW
V9	Interval Verificare	Integer	10	3600	300	s	RW
V10	Reset Defaults	Integer	0	1	0	—	Push button
V11	Marja Temperatură	Double	0	10	2	°C	RW
V12	Marja Umiditate	Double	0	20	5	%	RW
V20	Restart ESP32	Integer	0	1	0	—	Push button
V21	Free Heap	Integer	0	520	0	KB	Read-only
V22	Lock Owner	Integer	0	2	0	—	Read-only (0=none, 1=Blynk, 2=MQTT)
V23	Firmware Build	Integer	0	9999	0	—	Read-only
Setări critice pentru V5 și V6:

„Invalidate Value" = OFF
„Sync with latest server value" = ON
Asigură că nu au „Is Raw" bifat
2. DASHBOARD — Web
Mergi la Templates → Web Dashboard → Edit.

Secțiunea SENZORI
4 × Gauge sau Label widget:

Gauge #1: V1 — „Temp Stânga" | Min -10, Max 80 | Color cyan
Gauge #2: V2 — „Umid. Stânga" | Min 0, Max 100
Gauge #3: V3 — „Temp Dreapta" | Min -10, Max 80
Gauge #4: V4 — „Umid. Dreapta" | Min 0, Max 100
Secțiunea CONTROL VENTILATOARE
Switch #1 — „Vent Stânga":

Datastream: V5
On Value: 1 | Off Value: 0
Tip: Switch (toggle)
Switch #2 — „Vent Dreapta":

Datastream: V6
On Value: 1 | Off Value: 0
Secțiunea SETĂRI
Slider — „Temp start vent" (V7):

Min: 20 | Max: 80 | Step: 0.5 | Label: °C
Slider — „Umid start vent" (V8):

Min: 20 | Max: 100 | Step: 1 | Label: %
Slider — „Interval verificare" (V9):

Min: 10 | Max: 3600 | Step: 10 | Label: s
Slider — „Marja temp" (V11):

Min: 0 | Max: 10 | Step: 0.5 | Label: °C
Slider — „Marja umiditate" (V12):

Min: 0 | Max: 20 | Step: 1 | Label: %
Secțiunea SISTEM
Button — „Reset Defaults" (V10):

Mode: Push (nu Toggle!)
On Value: 1 | Off Value: 0
Button — „Restart ESP32" (V20):

Mode: Push
On Value: 1 | Off Value: 0
Label — „Free Heap" (V21): afișează {/v21} KB

Label — „Lock Owner" (V22): afișează {/v22} (0/1/2)

Label — „Firmware Build" (V23): afișează #{/v23}

3. DASHBOARD — Mobile (Blynk App)
Același layout, adaptate pentru mobile. Ordinea recomandată:


[Temp S: xx°C]  [Umid S: xx%]  [Temp D: xx°C]  [Umid D: xx%]

[  VENT STÂNGA  ●/○  ]    [  VENT DREAPTA  ●/○  ]

Temp start vent: ──●────  45.0°C
Umid start vent: ──●────  60%
Interval:        ──●────  300s
Marja temp:      ──●────  2.0°C
Marja umiditate: ──●────  5%

[RESET DEFAULTS]    [RESTART ESP32]

Free Heap: xxx KB  |  FW Build: #xxx  |  Lock: x
4. EVENTS
Mergi la Templates → Events → Add Event.

Event Name	Code	Notificare
Senzor eroare	sensor_error	Push ON
Override expirat	override_expired	Push OFF
Restart sistem	system_restart	Push ON
Comandă respinsă	cmd_rejected	Push OFF
5. CHECKLIST VERIFICARE
După ce salvezi template-ul și faci Publish:

Senzori vizibili: ESP32 conectat → V1/V2/V3/V4 se actualizează la fiecare interval
Toggle Vent Stânga (V5): apasă ON → releul pornește → toggle rămâne ON; apasă OFF → revenire auto
Reset Defaults: apasă butonul → ESP32 logează reset → sliderele V7/V8/V9/V11/V12 revin la valorile default (45°C / 60% / 300s / 2°C / 5%) → senzori continuă să se afișeze
Marja temp (V11): mișcă la 3.0°C → ESP32 logează [Blynk] Hysteresis temp: 3.0°C
Prag temp (V7): mișcă la 50°C → ESP32 confirmă → Android Settings reflectă 50°C
6. NOTE IMPORTANTE
Nu bifa „Is Raw" pe V5/V6 — altfel Blynk trimite valori brute și firmware-ul le poate ignora
Butonele V10/V20 TREBUIE să fie mod „Push" nu „Toggle" — altfel rămân pe 1 și ESP32 face reset în buclă
Sync la reconectare: firmware-ul trimite automat valorile curente la fiecare BLYNK_CONNECTED() — nu ai nevoie de „Sync" manual după repornire ESP32
Lock owner (V22): dacă afișează 1 timp îndelungat înseamnă că lock-ul a rămas blocat pe Blynk — repornește ESP32
pentru 1 datastreams vreau sa imi spui si ce widget sa folosesc la fiecare pin
Datastreams + Widget recomandat pentru fiecare pin
Pin	Nume	Tip datastream	Widget recomandat	Setări widget
V1	Temp Stânga	Double, -10…80, °C	Gauge	Min -10, Max 80, color cyan
V2	Umiditate Stânga	Double, 0…100, %	Gauge	Min 0, Max 100
V3	Temp Dreapta	Double, -10…80, °C	Gauge	Min -10, Max 80, color cyan
V4	Umiditate Dreapta	Double, 0…100, %	Gauge	Min 0, Max 100
V5	Vent Stânga	Integer, 0…1	Switch	On=1, Off=0, mod Toggle
V6	Vent Dreapta	Integer, 0…1	Switch	On=1, Off=0, mod Toggle
V7	Prag Temperatură	Double, 20…80, °C	Slider	Step 0.5
V8	Prag Umiditate	Double, 20…100, %	Slider	Step 1
V9	Interval Verificare	Integer, 10…3600, s	Slider	Step 10
V10	Reset Defaults	Integer, 0…1	Button	Mod Push, On=1, Off=0
V11	Marja Temperatură	Double, 0…10, °C	Slider	Step 0.5
V12	Marja Umiditate	Double, 0…20, %	Slider	Step 1
V20	Restart ESP32	Integer, 0…1	Button	Mod Push, On=1, Off=0
V21	Free Heap	Integer, 0…520, KB	Label	Format: {/v21} KB
V22	Lock Owner	Integer, 0…2	Label	Format: {/v22}
V23	Firmware Build	Integer, 0…9999	Label	Format: #{/v23}
Cel mai important detaliu: V10 și V20 — mod Push, nu Toggle. Altfel ESP32 intră în reset/restart loop.