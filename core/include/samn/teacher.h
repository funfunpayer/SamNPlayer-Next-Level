#pragma once
// Teachers for AI Train (D-155 step 4): a vision-language model on a server
// the user names (Ollama, LM Studio, any OpenAI-compatible address, e.g. a
// Mac in the home network) looks at a frame and proposes boxes. Plain JSON
// in and out; the network call lives in the app. Proposals are never
// confirmed: the user checks them in the label editor.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "samn/label_track.h"

namespace samn {

struct TeacherBox {
    std::string cls;  // one of the asked classes
    LabelBox box;     // video pixels
};

// What the model is asked: the classes, one JSON object back, boxes as
// fractions of the picture.
std::string TeacherPrompt(const std::vector<std::string>& classes);

// The /chat/completions body: the prompt and the frame as a data URL
// (OpenAI vision format, which Ollama and LM Studio both read).
std::string TeacherRequestBody(const std::string& model, const std::string& prompt, const std::string& jpeg_base64);

// The answer's text from a /chat/completions response; nullopt with *error
// set for an error response or something that is not one.
std::optional<std::string> ChatContent(const std::string& response_json, std::string* error = nullptr);

// The model names of a /models response ({"data":[{"id":…}]}), sorted.
std::vector<std::string> ParseModelList(const std::string& response_json);

// Boxes from the model's text. Tolerant: the JSON may sit in a code fence or
// prose; a box may be "box" [x, y, w, h] or "bbox" / "bbox_2d" [x1, y1, x2,
// y2]; values up to 1 are fractions, else pixels when they fit the picture,
// else 0..1000. Unknown classes and empty boxes are dropped; "hand 2" counts
// as "hand".
std::vector<TeacherBox> ParseTeacherBoxes(const std::string& text, double frame_w, double frame_h,
                                          const std::vector<std::string>& classes);

// Puts the teacher's boxes at ms as unconfirmed keys (source `source`): each
// box goes to the object of its class it overlaps most at ms, or to a new
// object; an object the user confirmed at ms is never touched. Returns how
// many keys were set.
int ApplyTeacherBoxes(LabelProject& p, int64_t ms, const std::vector<TeacherBox>& boxes, const std::string& source,
                      int64_t tolerance_ms = 0);

}  // namespace samn
