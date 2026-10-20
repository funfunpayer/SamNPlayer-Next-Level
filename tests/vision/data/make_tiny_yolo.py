"""Writes tiny_yolo.onnx: a stand-in for a YOLOv8 detector in the vision
tests (no trained weights, no pictures). Input "images" 1x3x64x64, output
"output0" 1x6x8: two classes, eight anchors, boxes in model pixels.

- anchor 0: box (cx 16, cy 16, w 8, h 8); its class-0 score is the mean of
  the red input channel, so a red frame finds it and a blue one does not
  (the colour order), and black padding lowers it (the square padding).
- anchor 1: class 1 at 0.9, box (48, 40, 16, 12).
- anchor 2: class 1 at 0.6, nearly the same box: NMS drops it.

Run: python3 make_tiny_yolo.py (needs the onnx package).
"""
import numpy as np
import onnx
from onnx import TensorProto, helper, numpy_helper

base = np.zeros((1, 6, 8), np.float32)
base[0, 0:4, 0] = [16, 16, 8, 8]
base[0, 0:4, 1] = [48, 40, 16, 12]
base[0, 5, 1] = 0.9
base[0, 0:4, 2] = [49, 40, 16, 12]
base[0, 5, 2] = 0.6
mask = np.zeros((1, 6, 8), np.float32)
mask[0, 4, 0] = 1

nodes = [
    helper.make_node("Gather", ["images", "red"], ["r"], axis=1),
    helper.make_node("ReduceMean", ["r"], ["m"], axes=[0, 1, 2], keepdims=0),
    helper.make_node("Mul", ["m", "mask"], ["scored"]),
    helper.make_node("Add", ["base", "scored"], ["output0"]),
]
graph = helper.make_graph(
    nodes,
    "tiny_yolo",
    [helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, 64, 64])],
    [helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, 6, 8])],
    [
        numpy_helper.from_array(np.array(0, np.int64), "red"),
        numpy_helper.from_array(base, "base"),
        numpy_helper.from_array(mask, "mask"),
    ],
)
model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], producer_name="samn-tests")
model.ir_version = 8
onnx.checker.check_model(model)
onnx.save(model, __file__.replace("make_tiny_yolo.py", "tiny_yolo.onnx"))
