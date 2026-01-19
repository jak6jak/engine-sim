extends Node

var runtime: Node

var starter_time_left := 2.5  # Longer starter time

@export var script_path := "res://assets/main.mr"
# speed_control: 0=full throttle, 1=idle (inverted because of DirectThrottleLinkage)
@export var speed_control := 0.9  # Start near idle
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

	runtime.set_speed_control(speed_control)
	runtime.set_ignition_enabled(true)  # Enable spark plugs!
	runtime.set_starter_enabled(true)
	runtime.set_audio_debug_enabled(true)
	runtime.set_audio_debug_interval(0.25)

	# Feed AudioStreamGenerator from the compiled GDExtension for better performance.
	runtime.start_audio(44100.0, 0.5)

func _process(delta: float) -> void:
	if starter_time_left > 0.0:
		starter_time_left -= delta
		if starter_time_left <= 0.0:
			runtime.set_starter_enabled(false)
			print("Starter disabled, RPM: %.1f" % runtime.get_engine_speed())
	
	# Throttle control: W/Up = more throttle (lower speed_control), S/Down = less throttle (higher speed_control)
	var throttle_input := 0.0
	if Input.is_key_pressed(KEY_W) or Input.is_key_pressed(KEY_UP):
		throttle_input -= 1.0  # W decreases speed_control = opens throttle
	if Input.is_key_pressed(KEY_S) or Input.is_key_pressed(KEY_DOWN):
		throttle_input += 1.0  # S increases speed_control = closes throttle
	
	if throttle_input != 0.0:
		speed_control = clamp(speed_control + throttle_input * throttle_sensitivity * delta, 0.0, 0.95)
		runtime.set_speed_control(speed_control)
		var actual_throttle := 1.0 - speed_control  # Approximate for display
		print("Throttle: %.0f%% RPM: %.1f" % [actual_throttle * 100, runtime.get_engine_speed()])
