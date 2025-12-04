import sys
import ast
from threading import Thread
from livelink.connect.livelink_init import create_socket_connection, initialize_py_face
from livelink.animations.default_animation import default_animation_loop, stop_default_animation
from utils.generated_runners import run_animation

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python run_neurosync.py path/to/blendshapes.txt [FaceSubjectName]")
        sys.exit(1)

    blendshape_file = sys.argv[1]
    actor_name = sys.argv[2] if len(sys.argv) > 2 else "face1"

    try:
        with open(blendshape_file, "r") as f:
            blendshape_data_str = f.read()
        blendshape_data = ast.literal_eval(blendshape_data_str)
    except Exception as e:
        print(f"Error reading blendshape file: {e}")
        sys.exit(1)

    py_face = initialize_py_face(name=actor_name)
    socket_connection = create_socket_connection()
    default_animation_thread = Thread(target=default_animation_loop, args=(py_face,))
    default_animation_thread.start()

    try:
        run_animation(blendshape_data, py_face, socket_connection, default_animation_thread)
    finally:
        stop_default_animation.set()
        if default_animation_thread:
            default_animation_thread.join()
        socket_connection.close()
