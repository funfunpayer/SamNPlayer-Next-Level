"""Writes tiny_yolo.onnx: a stand-in for a YOLOv8 detector in the vision
tests (no trained weights, no pictures). Input "images" 1x3x64x64, output
"output0" 1x6x8: two classes, eight anchors, boxes in model pixels.

- anchor 0: box (cx 16, cy 16, w 8, h 8); its class-0 score is the mean of
  the red input channel, so a red frame finds it and a blue one does not
  (the colour order), and black padding lowers it (the square padding).
- anchor 1: class 1 at 0.9, box (48, 40, 16, 12).
- anchor 2: class 1 at 0.6, nearly the same box: NMS drops it.

It also writes tiny_nudenet.onnx, shaped like NudeNet (18 classes, output
1x22x4) for the app's tests: anchor 0 is FACE_MALE at 0.9 (box 32, 32,
20, 20), anchor 1 FEET_EXPOSED at 0.9 (no label of ours).

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


def save(graph, name):
    model = helper.make_model(graph, opset_imports=[helper.make_opsetid("", 17)], producer_name="samn-tests")
    model.ir_version = 8
    onnx.checker.check_model(model)
    onnx.save(model, __file__.replace("make_tiny_yolo.py", name))


save(graph, "tiny_yolo.onnx")

nn = np.zeros((1, 22, 4), np.float32)
nn[0, 0:4, 0] = [32, 32, 20, 20]
nn[0, 4 + 12, 0] = 0.9  # FACE_MALE
nn[0, 0:4, 1] = [10, 50, 8, 8]
nn[0, 4 + 7, 1] = 0.9  # FEET_EXPOSED
save(
    helper.make_graph(
        [
            helper.make_node("ReduceMean", ["images"], ["m"], axes=[0, 1, 2, 3], keepdims=0),
            helper.make_node("Mul", ["m", "zero"], ["z"]),
            helper.make_node("Add", ["base", "z"], ["output0"]),
        ],
        "tiny_nudenet",
        [helper.make_tensor_value_info("images", TensorProto.FLOAT, [1, 3, 64, 64])],
        [helper.make_tensor_value_info("output0", TensorProto.FLOAT, [1, 22, 4])],
        [numpy_helper.from_array(nn, "base"), numpy_helper.from_array(np.array(0, np.float32), "zero")],
    ),
    "tiny_nudenet.onnx",
)

# tiny_detr.onnx: shaped like an RF-DETR export (input 1x3x64x64, outputs
# "dets" 1x4x4 and "labels" 1x4x3). Query 0 is class 1 at logit 3 (0.95),
# box (0.25, 0.5, 0.2, 0.4) as fractions; query 1's class-0 logit is the mean
# of the normalised red channel (red frame: +2.25, 0.90; blue: -2.12, 0.11),
# box (0.75, 0.25, 0.1, 0.1); queries 2 and 3 are below any threshold.
dets = np.array([[[0.25, 0.5, 0.2, 0.4], [0.75, 0.25, 0.1, 0.1], [0.5, 0.5, 0.1, 0.1], [0.5, 0.5, 0.2, 0.2]]], np.float32)
logits = np.full((1, 4, 3), -6.0, np.float32)
logits[0, 0, 1] = 3.0
logits[0, 1, 0] = 0.0  # plus the red mean
redmask = np.zeros((1, 4, 3), np.float32)
redmask[0, 1, 0] = 1
save(
    helper.make_graph(
        [
            helper.make_node("Gather", ["input", "red"], ["r"], axis=1),
            helper.make_node("ReduceMean", ["r"], ["m"], axes=[0, 1, 2], keepdims=0),
            helper.make_node("Mul", ["m", "redmask"], ["scored"]),
            helper.make_node("Add", ["logits", "scored"], ["labels"]),
            helper.make_node("Identity", ["dets_c"], ["dets"]),
        ],
        "tiny_detr",
        [helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 3, 64, 64])],
        [
            helper.make_tensor_value_info("dets", TensorProto.FLOAT, [1, 4, 4]),
            helper.make_tensor_value_info("labels", TensorProto.FLOAT, [1, 4, 3]),
        ],
        [
            numpy_helper.from_array(np.array(0, np.int64), "red"),
            numpy_helper.from_array(dets, "dets_c"),
            numpy_helper.from_array(logits, "logits"),
            numpy_helper.from_array(redmask, "redmask"),
        ],
    ),
    "tiny_detr.onnx",
)
