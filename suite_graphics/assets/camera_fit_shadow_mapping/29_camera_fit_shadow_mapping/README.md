# Camera-fit shadow-mapping scene

Scene and original shaders: Alex Link, from
`motor_suites/techniques/working/02_camera_fit_shadow_mapping`.

Used with the author's request to bring the test into motor_ai_suites.
The exported Blender scene and its `scene.bin` retain their original contents.
The local shadow-light shader adds a modest ambient term, a bounded 3 x 3 PCF
lookup and explicit MRT outputs for this demo. These are sample assets, not
changes to motor or the original motor_suites test.

The position/normal MRT shader originates from the discontinued RSM experiment;
this application does shadow mapping only, not RSM indirect illumination.
