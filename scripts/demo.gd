extends Node

var runtime: Node

var starter_time_left := 8.0  # Longer starter time for slow-cranking diesel engines

@export var script_path := "res://assets/main.mr"
@export var speed_control := 1.0  # Full throttle helps diesel catch
@export var throttle_sensitivity := 2.0  # How fast throttle responds per second

func _ready() -> void:
	runtime = EngineSimRuntime.new()
	add_child(runtime)

	var script_to_load := script_path
	if not FileAccess.file_exists(script_to_load):
		push_warning("Can't find main.mr at: %s" % script_to_load)

	var ok = runtime.load_mr_script(script_to_load)
	if not ok:
		push_error("Failed to load script: %s" % script_to_load)

	# Set neutral gear and disengage clutch (same as test)
	runtime.set_gear(-1)  # Neutral
	runtime.set_clutch_pressure(0.0)  # Clutch disengaged
	
	runtime.set_speed_control(speed_control)
	runtime.set_ignition_enabled(true)  # Enable spark plugs!
	runtime.set_starter_enabled(true)
	runtime.set_audio_debug_enabled(true)
	runtime.set_audio_debug_interval(1.0)
	
	# Run simulation 10% faster than real-time to build audio buffer headroom
	runtime.set_simulation_speed(1.1)

	# Start audio with Godot's mix rate
	var mix_rate := AudioServer.get_mix_rate()
	runtime.start_audio(mix_rate, 0.5)  # Increased buffer for smoother audio

	print("Engine sim demo started!")
	print("  Script: ", script_to_load)
	print("  Mix rate: ", mix_rate)
	print("  Use Up/Down arrow keys to control throttle")
	print("  Initial speed_control: ", speed_control)

func _process(delta: float) -> void:
	# Handle starter motor timing
	if starter_time_left > 0.0:
		starter_time_left -= delta
		if starter_time_left <= 0.0:
			runtime.set_starter_enabled(false)
			print("Starter disabled")

	# Log RPM periodically
	var rpm = runtime.get_engine_speed()
	if Engine.get_frames_drawn() % 60 == 0:
		print("RPM: %.1f  speed_control: %.2f" % [rpm, speed_control])

	# Control throttle with arrow keys
	var throttle_input := 0.0
	if Input.is_action_pressed("ui_up"):
		throttle_input = 1.0
	elif Input.is_action_pressed("ui_down"):
		throttle_input = -1.0

	if throttle_input != 0.0:
		speed_control = clamp(speed_control + throttle_input * throttle_sensitivity * delta, 0.0, 1.0)
		runtime.set_speed_control(speed_control)

	# Optional: display current RPM
	if Input.is_action_just_pressed("ui_select"):
		rpm = runtime.get_engine_speed()
		print("Engine speed: %.1f RPM" % rpm)
