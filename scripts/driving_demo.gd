extends Node

## Full driving interaction demo for engine-sim
## Controls:
##   W/Up Arrow    - Throttle up
##   S/Down Arrow  - Throttle down / Brake
##   A/D or L/R    - (Reserved for steering)
##   Shift         - Shift up
##   Ctrl          - Shift down
##   Space         - Clutch (hold)
##   E             - Toggle ignition
##   R             - Start engine (hold)
##   N             - Neutral gear
##   Tab           - Toggle automatic transmission

var runtime: Node

# Engine script path
@export var script_path := "res://assets/main.mr"
@export var auto_start := true  # Automatically start the engine

# ===== Transmission =====
var gear: int = 0  # 0 = neutral, 1-6 = gears, -1 = reverse
var max_gear: int = 6
var gear_ratios: Array[float] = [0.0, 3.5, 2.2, 1.5, 1.1, 0.9, 0.75]  # [N, 1st, 2nd, 3rd, 4th, 5th, 6th]
var reverse_ratio: float = -3.8
var final_drive: float = 3.7
var automatic_mode: bool = true
var shift_cooldown: float = 0.0
const SHIFT_COOLDOWN_TIME := 0.5  # Increased for smoother shifts

# Debug timers
var auto_shift_debug_timer := 0.0
var baseline_debug_timer := 0.0

# ===== Auto Transmission Tuning =====
const UPSHIFT_RPM := 5500.0
const DOWNSHIFT_RPM := 2000.0
const UPSHIFT_HYSTERESIS := 300.0  # Prevent rapid upshifting
const DOWNSHIFT_HYSTERESIS := 300.0  # Prevent rapid downshifting
const AUTO_SHIFT_THROTTLE_THRESHOLD := 0.05  # Lower threshold to enter first gear more eagerly

# ===== Clutch =====
var clutch_engaged: float = 1.0  # 1.0 = fully engaged, 0.0 = fully disengaged
var clutch_target: float = 1.0
const CLUTCH_ENGAGE_SPEED := 3.0  # How fast clutch engages per second

# ===== Throttle =====
var throttle: float = 0.5  # 0.0 = closed, 1.0 = full (for HUD display)
var speed_control: float = 1.0 # 0.1=near idle, 1.0=full throttle (for engine-sim API)
const THROTTLE_RESPONSE := 1.0  # How fast throttle responds per second

# ===== Vehicle Physics (simplified) =====
var vehicle_speed_kmh: float = 0.0  # km/h
var wheel_rpm: float = 0.0
const WHEEL_CIRCUMFERENCE := 2.0  # meters (roughly 205/55R16 tire)
const VEHICLE_MASS := 1400.0  # kg
const DRAG_COEFFICIENT := 0.35
const ROLLING_RESISTANCE := 0.015

# ===== Engine State =====
var engine_running: bool = false
var ignition_on: bool = false
var starter_held: bool = false
var engine_rpm: float = 0.0
var starter_time_left := 8.0
const IDLE_RPM := 2800.0
const REDLINE_RPM := 7000.0
const STALL_RPM := 400.0

# ===== UI References =====
var hud: Control

func _ready() -> void:
	# Create engine runtime
	runtime = EngineSimRuntime.new()
	add_child(runtime)
	
	# Load engine script
	var script_to_load := script_path
	print("Loading engine script: ", script_to_load)
	
	if not FileAccess.file_exists(script_to_load):
		push_error("Can't find script at: %s" % script_to_load)
		return
	
	var ok = runtime.load_mr_script(script_to_load)
	if not ok:
		push_error("Failed to load script: %s" % script_to_load)
		return
	
	print("Engine script loaded successfully")
	
	# Set initial engine state BEFORE starting audio (important order!)
	# Must set gear and clutch FIRST, then speed_control, then ignition/starter
	runtime.set_gear(-1)  # Start in neutral (engine-sim uses -1 for neutral)
	runtime.set_clutch_pressure(0.0)  # Clutch disengaged for startup
	runtime.set_speed_control(speed_control)  # Full throttle (1.0) helps diesel catch
	runtime.set_ignition_enabled(true)  # Enable spark plugs
	runtime.set_starter_enabled(true)  # Start cranking
	runtime.set_audio_debug_enabled(true)
	runtime.set_audio_debug_interval(2.0)
	runtime.set_simulation_speed(1.1)  # 10% faster for audio buffer headroom
	
	# Now start audio (this runs prefill with proper engine state)
	var mix_rate := AudioServer.get_mix_rate()
	runtime.start_audio(mix_rate, 0.5)
	
	# Set engine state tracking
	ignition_on = true
	engine_running = false  # Will become true once RPM is high enough
	starter_held = true
	
	# Create HUD
	_create_hud()
	
	print("=== Engine Sim Driving Demo ===")
	print("Build: 2026-01-19")
	print("Mix rate: ", mix_rate)
	print("Auto-start: ", auto_start)
	print("Controls:")
	print("  W/Up    - Throttle")
	print("  S/Down  - Brake/Decelerate")
	print("  Shift   - Shift Up")
	print("  Ctrl    - Shift Down")
	print("  Space   - Clutch (hold)")
	print("  E       - Toggle Ignition")
	print("  R       - Starter Motor (hold)")
	print("  Tab     - Toggle Auto/Manual")
	print("================================")

func _create_hud() -> void:
	hud = Control.new()
	hud.set_anchors_preset(Control.PRESET_FULL_RECT)
	add_child(hud)
	
	# Background panel
	var panel = Panel.new()
	panel.position = Vector2(20, 20)
	panel.size = Vector2(300, 200)
	var style = StyleBoxFlat.new()
	style.bg_color = Color(0.1, 0.1, 0.1, 0.8)
	style.corner_radius_top_left = 10
	style.corner_radius_top_right = 10
	style.corner_radius_bottom_left = 10
	style.corner_radius_bottom_right = 10
	panel.add_theme_stylebox_override("panel", style)
	hud.add_child(panel)
	
	# Create info labels container
	var vbox = VBoxContainer.new()
	vbox.position = Vector2(10, 10)
	vbox.size = Vector2(280, 180)
	panel.add_child(vbox)
	
	# RPM display
	var rpm_label = Label.new()
	rpm_label.name = "RPMLabel"
	rpm_label.text = "RPM: 0"
	rpm_label.add_theme_font_size_override("font_size", 24)
	vbox.add_child(rpm_label)
	
	# Speed display
	var speed_label = Label.new()
	speed_label.name = "SpeedLabel"
	speed_label.text = "Speed: 0 km/h"
	speed_label.add_theme_font_size_override("font_size", 20)
	vbox.add_child(speed_label)
	
	# Gear display
	var gear_label = Label.new()
	gear_label.name = "GearLabel"
	gear_label.text = "Gear: N"
	gear_label.add_theme_font_size_override("font_size", 20)
	vbox.add_child(gear_label)
	
	# Throttle bar
	var throttle_container = HBoxContainer.new()
	var throttle_text = Label.new()
	throttle_text.text = "Throttle: "
	throttle_container.add_child(throttle_text)
	var throttle_bar = ProgressBar.new()
	throttle_bar.name = "ThrottleBar"
	throttle_bar.min_value = 0.0
	throttle_bar.max_value = 1.0
	throttle_bar.custom_minimum_size = Vector2(150, 20)
	throttle_bar.show_percentage = false
	throttle_container.add_child(throttle_bar)
	vbox.add_child(throttle_container)
	
	# Clutch bar
	var clutch_container = HBoxContainer.new()
	var clutch_text = Label.new()
	clutch_text.text = "Clutch:   "
	clutch_container.add_child(clutch_text)
	var clutch_bar = ProgressBar.new()
	clutch_bar.name = "ClutchBar"
	clutch_bar.min_value = 0.0
	clutch_bar.max_value = 1.0
	clutch_bar.custom_minimum_size = Vector2(150, 20)
	clutch_bar.show_percentage = false
	clutch_container.add_child(clutch_bar)
	vbox.add_child(clutch_container)
	
	# Status display
	var status_label = Label.new()
	status_label.name = "StatusLabel"
	status_label.text = "Ignition: OFF"
	status_label.add_theme_color_override("font_color", Color.RED)
	vbox.add_child(status_label)
	
	# Transmission mode
	var mode_label = Label.new()
	mode_label.name = "ModeLabel"
	mode_label.text = "Mode: AUTO"
	vbox.add_child(mode_label)

func _process(delta: float) -> void:
	if runtime == null:
		return
	
	# Handle starter motor timing (like demo.gd)
	if starter_time_left > 0.0:
		starter_time_left -= delta
		if starter_time_left <= 0.0:
			runtime.set_starter_enabled(false)
			starter_held = false
			print("Starter disabled (timeout)")
	
	# Update shift cooldown
	if shift_cooldown > 0:
		shift_cooldown -= delta

	# Baseline debug to confirm processing
	baseline_debug_timer += delta
	if baseline_debug_timer >= 0.5:
		baseline_debug_timer = 0.0
		print("[proc] auto=%s running=%s gear=%d simGear=%d rpm=%.0f speed=%.1f throttle=%.2f clutch=%.2f cooldown=%.2f" % [
			automatic_mode,
			engine_running,
			gear,
			gear - 1,
			engine_rpm,
			vehicle_speed_kmh,
			throttle,
			clutch_engaged,
			max(shift_cooldown, 0.0)
		])
	
	# Handle input
	_handle_input(delta)
	
	# Update clutch
	clutch_engaged = move_toward(clutch_engaged, clutch_target, CLUTCH_ENGAGE_SPEED * delta)
	
	# Sync clutch to engine-sim transmission
	# In neutral (gear=0, engine-sim gear=-1), clutch is disengaged so engine idles freely
	# In gear, clutch engages to put load on the engine, with gentle engagement in 1st gear
	if gear == 0:
		runtime.set_clutch_pressure(0.0)  # Neutral = clutch disengaged
	elif gear == 1 and engine_rpm < IDLE_RPM * 1.1:
		# When in 1st gear but RPM is barely above idle, keep clutch mostly disengaged
		runtime.set_clutch_pressure(clutch_engaged * 0.3)  # Gentle engagement
	else:
		runtime.set_clutch_pressure(clutch_engaged)
	
	# Get engine RPM
	engine_rpm = runtime.get_engine_speed()
	
	# Auto-start: check if engine has caught
	if starter_held and auto_start and engine_rpm > IDLE_RPM * 0.7:
		engine_running = true
		_stop_starter()
		print("Engine started! RPM: %.0f" % engine_rpm)

	# Failsafe: if RPM is above idle for any reason, consider engine running
	if not engine_running and engine_rpm > IDLE_RPM * 0.6:
		engine_running = true
	
	# Check for stall
	if engine_running and engine_rpm < STALL_RPM and not starter_held:
		_stall_engine()
	
	# Update vehicle physics
	_update_vehicle_physics(delta)
	
	# Auto transmission logic - allow shifting once RPM is alive even if engine_running flag missed
	if automatic_mode:
		_auto_shift()
		_auto_shift_debug(delta)
	
	# Apply throttle to engine via speed_control
	# speed_control: 0.0 = idle, 1.0 = full throttle
	runtime.set_speed_control(speed_control)
	
	# Update HUD
	_update_hud()

func _handle_input(delta: float) -> void:
	# Throttle input - directly modify speed_control like original demo
	# W/Up = increase speed_control = more throttle
	# S/Down = decrease speed_control = less throttle
	var throttle_dir := 0.0
	if Input.is_action_pressed("ui_up"):
		throttle_dir = 1.0  # Increase speed_control = more throttle
	elif Input.is_action_pressed("ui_down"):
		throttle_dir = -1.0  # Decrease speed_control = less throttle
	
	if throttle_dir != 0.0:
		speed_control = clamp(speed_control + throttle_dir * THROTTLE_RESPONSE * delta, 0.0, 1.0)
	
	# Update throttle variable for HUD (same as speed_control now)
	throttle = speed_control
	
	# Always apply current speed_control to engine
	runtime.set_speed_control(speed_control)
	
	# Clutch (space = disengage)
	if Input.is_key_pressed(KEY_SPACE):
		clutch_target = 0.0
	else:
		clutch_target = 1.0
	
	# Ignition toggle (E key)
	if Input.is_key_pressed(KEY_E):
		if not get_meta("e_was_pressed", false):
			set_meta("e_was_pressed", true)
			_toggle_ignition()
	else:
		set_meta("e_was_pressed", false)
	
	# Note: Starter is controlled automatically by fixed timer (8 seconds)
	# Manual R key control removed to prevent interference with auto_start
	
	# Shift up (Left Shift)
	if Input.is_key_pressed(KEY_SHIFT) and shift_cooldown <= 0:
		if not get_meta("shift_was_pressed", false):
			set_meta("shift_was_pressed", true)
			_shift_up()
	else:
		set_meta("shift_was_pressed", false)
	
	# Shift down (Ctrl)
	if Input.is_key_pressed(KEY_CTRL) and shift_cooldown <= 0:
		if not get_meta("ctrl_was_pressed", false):
			set_meta("ctrl_was_pressed", true)
			_shift_down()
	else:
		set_meta("ctrl_was_pressed", false)
	
	# Neutral (N)
	if Input.is_key_pressed(KEY_N):
		if not get_meta("n_was_pressed", false):
			set_meta("n_was_pressed", true)
			gear = 0
			runtime.set_gear(-1)  # engine-sim uses -1 for neutral
			print("Gear: N")
	else:
		set_meta("n_was_pressed", false)
	
	# Toggle auto/manual (Tab)
	if Input.is_key_pressed(KEY_TAB):
		if not get_meta("tab_was_pressed", false):
			set_meta("tab_was_pressed", true)
			automatic_mode = not automatic_mode
			print("Transmission: ", "AUTO" if automatic_mode else "MANUAL")
	else:
		set_meta("tab_was_pressed", false)

func _toggle_ignition() -> void:
	ignition_on = not ignition_on
	runtime.set_ignition_enabled(ignition_on)
	print("Ignition: ", "ON" if ignition_on else "OFF")
	
	if not ignition_on:
		engine_running = false
		_stop_starter()

func _start_starter() -> void:
	starter_held = true
	runtime.set_starter_enabled(true)
	print("Starter: ON")

func _stop_starter() -> void:
	starter_held = false
	runtime.set_starter_enabled(false)
	print("Starter: OFF")

func _stall_engine() -> void:
	engine_running = false
	print("ENGINE STALLED!")

func _shift_up() -> void:
	if gear < max_gear:
		gear += 1
		# Map GDScript gear (0=N, 1-6=gears) to engine-sim gear (-1=N, 0-5=gears)
		runtime.set_gear(gear - 1)
		shift_cooldown = SHIFT_COOLDOWN_TIME
		print("Gear: ", _gear_string())

func _shift_down() -> void:
	if gear > -1:  # Allow going to reverse
		gear -= 1
		# Map GDScript gear to engine-sim gear
		runtime.set_gear(gear - 1)
		shift_cooldown = SHIFT_COOLDOWN_TIME
		print("Gear: ", _gear_string())

func _gear_string() -> String:
	if gear == 0:
		return "N"
	elif gear == -1:
		return "R"
	else:
		return str(gear)

func _auto_shift() -> void:
	# Don't shift if cooldown active or clutch mostly disengaged
	if shift_cooldown > 0 or clutch_engaged < 0.5:
		return
	
	# CRITICAL: Don't shift until engine is actually running
	# This prevents loading the engine during startup
	if not engine_running:
		return
	
	# Start in first gear when moving from neutral
	if gear <= 0:
		if throttle > AUTO_SHIFT_THROTTLE_THRESHOLD or engine_rpm > IDLE_RPM * 1.2:
			gear = 1
			runtime.set_gear(0)  # engine-sim gear 0 = first gear
			shift_cooldown = SHIFT_COOLDOWN_TIME
			print("Auto shift to 1st (RPM: %.0f, throttle: %.2f)" % [engine_rpm, throttle])
		return
	
	# Upshift logic - smart RPM-based shifting with throttle consideration
	if engine_rpm > UPSHIFT_RPM + UPSHIFT_HYSTERESIS and gear < max_gear:
		# More aggressive upshifting at lower throttle positions
		# This keeps engine in good power band during cruising
		if throttle < 0.3 or engine_rpm > UPSHIFT_RPM * 1.15:
			gear += 1
			runtime.set_gear(gear - 1)
			shift_cooldown = SHIFT_COOLDOWN_TIME
			print("Auto upshift: gear %d (RPM: %.0f, speed: %.1f km/h)" % [gear, engine_rpm, vehicle_speed_kmh])
	
	# Downshift logic - help engine braking and keep RPM in power band
	elif engine_rpm < DOWNSHIFT_RPM - DOWNSHIFT_HYSTERESIS and gear > 1:
		# Downshift if going too slow for current gear
		gear -= 1
		runtime.set_gear(gear - 1)
		shift_cooldown = SHIFT_COOLDOWN_TIME
		print("Auto downshift: gear %d (RPM: %.0f, speed: %.1f km/h)" % [gear, engine_rpm, vehicle_speed_kmh])
	elif vehicle_speed_kmh < 8.0 and gear > 1:
		# Force drop to 1st at crawl speeds even if RPM sensor is noisy
		gear = 1
		runtime.set_gear(0)
		shift_cooldown = SHIFT_COOLDOWN_TIME
		print("Auto downshift (crawl): gear %d (RPM: %.0f, speed: %.1f km/h)" % [gear, engine_rpm, vehicle_speed_kmh])

func _auto_shift_debug(delta: float) -> void:
	auto_shift_debug_timer += delta
	if auto_shift_debug_timer < 0.5:
		return
	auto_shift_debug_timer = 0.0
	print("[auto] gear=%d simGear=%d rpm=%.0f speed=%.1f throttle=%.2f clutch=%.2f cooldown=%.2f" % [
		gear,
		gear - 1,
		engine_rpm,
		vehicle_speed_kmh,
		throttle,
		clutch_engaged,
		max(shift_cooldown, 0.0)
	])

func _update_vehicle_physics(delta: float) -> void:
	if gear == 0 or not engine_running:
		# Neutral or engine off - coast with drag
		var drag_force = DRAG_COEFFICIENT * vehicle_speed_kmh * vehicle_speed_kmh * 0.001
		var rolling = ROLLING_RESISTANCE * VEHICLE_MASS * 0.01
		var decel = (drag_force + rolling) / VEHICLE_MASS * 3.6  # Convert to km/h
		vehicle_speed_kmh = max(0, vehicle_speed_kmh - decel * delta)
	else:
		# Calculate gear ratio
		var ratio: float
		if gear == -1:
			ratio = reverse_ratio
		else:
			ratio = gear_ratios[gear]
		
		var total_ratio = ratio * final_drive
		
		# Calculate wheel RPM from vehicle speed
		wheel_rpm = (vehicle_speed_kmh / 3.6) / WHEEL_CIRCUMFERENCE * 60.0  # speed(m/s) / circumference * 60
		
		# Calculate expected engine RPM based on speed and gear
		var expected_engine_rpm = wheel_rpm * total_ratio
		
		# Engine produces torque based on actual engine RPM and throttle
		# Simplified torque curve: torque increases with RPM up to peak, then decreases
		var engine_torque = 0.0
		if engine_running:
			# Simple torque curve: peaks around 5000 RPM with max 350 Nm
			var rpm_ratio = engine_rpm / REDLINE_RPM
			var torque_factor = max(0.0, 1.0 - pow(rpm_ratio - 0.7, 2.0) * 2.0)  # Peak around 70% redline
			engine_torque = throttle * 350.0 * torque_factor * clutch_engaged
		
		# Wheel torque
		var wheel_torque = engine_torque * total_ratio * 0.9  # 90% drivetrain efficiency
		
		# Force at wheels
		var wheel_force = wheel_torque / (WHEEL_CIRCUMFERENCE / (2 * PI))
		
		# Drag and rolling resistance
		var drag_force = DRAG_COEFFICIENT * vehicle_speed_kmh * vehicle_speed_kmh * 0.01
		var rolling = ROLLING_RESISTANCE * VEHICLE_MASS * 9.81
		
		# Net force
		var net_force = wheel_force - drag_force - rolling
		if gear == -1:
			net_force = -net_force
		
		# Acceleration
		var accel = net_force / VEHICLE_MASS
		vehicle_speed_kmh += accel * delta * 3.6  # Convert m/s² to km/h/s
		vehicle_speed_kmh = max(0, vehicle_speed_kmh)  # No negative speed (for now)

func _update_hud() -> void:
	if hud == null:
		return
	
	# Find labels
	var rpm_label = hud.find_child("RPMLabel", true, false) as Label
	var speed_label = hud.find_child("SpeedLabel", true, false) as Label
	var gear_label = hud.find_child("GearLabel", true, false) as Label
	var throttle_bar = hud.find_child("ThrottleBar", true, false) as ProgressBar
	var clutch_bar = hud.find_child("ClutchBar", true, false) as ProgressBar
	var status_label = hud.find_child("StatusLabel", true, false) as Label
	var mode_label = hud.find_child("ModeLabel", true, false) as Label
	
	if rpm_label:
		rpm_label.text = "RPM: %d" % int(engine_rpm)
		# Color based on RPM
		if engine_rpm > REDLINE_RPM * 0.9:
			rpm_label.add_theme_color_override("font_color", Color.RED)
		elif engine_rpm > REDLINE_RPM * 0.7:
			rpm_label.add_theme_color_override("font_color", Color.YELLOW)
		else:
			rpm_label.add_theme_color_override("font_color", Color.WHITE)
	
	if speed_label:
		speed_label.text = "Speed: %d km/h" % int(vehicle_speed_kmh)
	
	if gear_label:
		gear_label.text = "Gear: %s" % _gear_string()
		if gear == -1:
			gear_label.add_theme_color_override("font_color", Color.RED)
		elif gear == 0:
			gear_label.add_theme_color_override("font_color", Color.YELLOW)
		else:
			gear_label.add_theme_color_override("font_color", Color.GREEN)
	
	if throttle_bar:
		throttle_bar.value = throttle
	
	if clutch_bar:
		clutch_bar.value = clutch_engaged
	
	if status_label:
		if engine_running:
			status_label.text = "Engine: RUNNING"
			status_label.add_theme_color_override("font_color", Color.GREEN)
		elif ignition_on:
			status_label.text = "Ignition: ON"
			status_label.add_theme_color_override("font_color", Color.YELLOW)
		else:
			status_label.text = "Ignition: OFF"
			status_label.add_theme_color_override("font_color", Color.RED)
	
	if mode_label:
		mode_label.text = "Mode: %s" % ("AUTO" if automatic_mode else "MANUAL")
