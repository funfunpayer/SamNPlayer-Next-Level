//go:build cgo && opencv

#include "cv.h"

#include <opencv2/opencv.hpp>
#include <opencv2/tracking.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/video/tracking.hpp>

struct VideoCapture_ {
    cv::VideoCapture cap;
    cv::Mat frame;
};

struct VideoWriter_ {
    cv::VideoWriter writer;
};

struct Mat_ {
    cv::Mat mat;
};

struct Tracker_ {
    cv::Ptr<cv::TrackerCSRT> tracker;
};

extern "C" {

VideoCapture VideoCapture_Open(const char *path) {
    VideoCapture_ *v = new VideoCapture_();
    v->cap.open(std::string(path));
    return v;
}

void VideoCapture_Close(VideoCapture v) { delete v; }

int VideoCapture_Read(VideoCapture v) {
    return v->cap.read(v->frame) ? 1 : 0;
}

int VideoCapture_IsOpened(VideoCapture v) {
    return v->cap.isOpened() ? 1 : 0;
}

double VideoCapture_Get(VideoCapture v, int propId) {
    return v->cap.get(propId);
}

void VideoCapture_Seek(VideoCapture v, int frameIndex) {
    v->cap.set(cv::CAP_PROP_POS_FRAMES, frameIndex);
}

VideoWriter VideoWriter_Open(const char *path, double fps, int width, int height) {
    VideoWriter_ *w = new VideoWriter_();
    int fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
    w->writer.open(std::string(path), fourcc, fps, cv::Size(width, height), true);
    return w;
}

void VideoWriter_Close(VideoWriter w) { delete w; }

void VideoWriter_WriteBGR(VideoWriter w, const unsigned char *data, int width, int height, int step) {
    cv::Mat frame(height, width, CV_8UC3, (void *)data, step);
    w->writer.write(frame);
}

MatHandle Frame_ToGray(VideoCapture v) {
    Mat_ *m = new Mat_();
    cv::cvtColor(v->frame, m->mat, cv::COLOR_BGR2GRAY);
    return m;
}

void Mat_Close(MatHandle m) { delete m; }
int Mat_Width(MatHandle m) { return m->mat.cols; }
int Mat_Height(MatHandle m) { return m->mat.rows; }

double Gray_HistCorrelation(MatHandle a, MatHandle b) {
    int histSize = 64;
    int channels[] = {0};
    float range[] = {0, 256};
    const float *ranges[] = {range};
    cv::Mat hist1, hist2;
    cv::calcHist(&a->mat, 1, channels, cv::Mat(), hist1, 1, &histSize, ranges);
    cv::calcHist(&b->mat, 1, channels, cv::Mat(), hist2, 1, &histSize, ranges);
    cv::normalize(hist1, hist1);
    cv::normalize(hist2, hist2);
    return cv::compareHist(hist1, hist2, cv::HISTCMP_CORREL);
}

double Gray_SignatureDiff(MatHandle a, MatHandle b) {
    cv::Mat sigA, sigB;
    cv::resize(a->mat, sigA, cv::Size(64, 48), 0, 0, cv::INTER_AREA);
    cv::resize(b->mat, sigB, cv::Size(64, 48), 0, 0, cv::INTER_AREA);
    sigA.convertTo(sigA, CV_32F);
    sigB.convertTo(sigB, CV_32F);
    cv::Mat diff;
    cv::absdiff(sigA, sigB, diff);
    return cv::mean(diff)[0];
}

double EstimateCameraMotionYExcludes(MatHandle prevGray, MatHandle gray,
                                      const int *exX, const int *exY,
                                      const int *exW, const int *exH, int n) {
    int h = prevGray->mat.rows, w = prevGray->mat.cols;
    cv::Mat mask = cv::Mat::ones(h, w, CV_8U) * 255;
    int pad = 10;
    for (int i = 0; i < n; i++) {
        int x0 = std::max(0, exX[i] - pad), y0 = std::max(0, exY[i] - pad);
        int x1 = std::min(w, exX[i] + exW[i] + pad), y1 = std::min(h, exY[i] + exH[i] + pad);
        if (x1 > x0 && y1 > y0) {
            mask(cv::Rect(x0, y0, x1 - x0, y1 - y0)).setTo(0);
        }
    }

    std::vector<cv::Point2f> prevPts;
    cv::goodFeaturesToTrack(prevGray->mat, prevPts, 200, 0.01, 20, mask, 7);
    if (prevPts.size() < 10) return 0.0;

    std::vector<cv::Point2f> currPts;
    std::vector<uchar> status;
    std::vector<float> err;
    cv::calcOpticalFlowPyrLK(prevGray->mat, gray->mat, prevPts, currPts, status, err);

    std::vector<cv::Point2f> goodPrev, goodCurr;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            goodPrev.push_back(prevPts[i]);
            goodCurr.push_back(currPts[i]);
        }
    }
    if (goodPrev.size() < 10) return 0.0;

    cv::Mat inliers;
    cv::Mat transform = cv::estimateAffinePartial2D(goodPrev, goodCurr, inliers, cv::RANSAC);
    if (transform.empty()) return 0.0;
    return transform.at<double>(1, 2);
}

double EstimateCameraMotionY(MatHandle prevGray, MatHandle gray,
                              int exX, int exY, int exW, int exH) {
    return EstimateCameraMotionYExcludes(prevGray, gray, &exX, &exY, &exW, &exH, 1);
}

MatHandle Gray_ExtractTemplate(MatHandle gray, int x, int y, int w, int h, double downscale) {
    x = std::max(0, x);
    y = std::max(0, y);
    int maxW = gray->mat.cols - x, maxH = gray->mat.rows - y;
    w = std::min(w, maxW);
    h = std::min(h, maxH);
    if (w < 8 || h < 8) return nullptr;

    cv::Mat patch = gray->mat(cv::Rect(x, y, w, h));
    Mat_ *m = new Mat_();
    if (downscale != 1.0) {
        cv::resize(patch, m->mat, cv::Size(), downscale, downscale, cv::INTER_AREA);
    } else {
        m->mat = patch.clone();
    }
    if (m->mat.rows < 4 || m->mat.cols < 4) {
        delete m;
        return nullptr;
    }
    return m;
}

MatHandle Gray_Downscale(MatHandle gray, double downscale) {
    Mat_ *m = new Mat_();
    if (downscale != 1.0) {
        cv::resize(gray->mat, m->mat, cv::Size(), downscale, downscale, cv::INTER_AREA);
    } else {
        m->mat = gray->mat.clone();
    }
    return m;
}

int Gray_MatchTemplate(MatHandle frame, MatHandle tmpl, int *outX, int *outY, double *outScore) {
    if (tmpl->mat.rows >= frame->mat.rows || tmpl->mat.cols >= frame->mat.cols) {
        return 0;
    }
    cv::Mat result;
    cv::matchTemplate(frame->mat, tmpl->mat, result, cv::TM_CCOEFF_NORMED);
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    *outX = maxLoc.x;
    *outY = maxLoc.y;
    *outScore = maxVal;
    return 1;
}

void Gray_FlowCells(MatHandle prevGray, MatHandle gray, int gw, int gh,
                    float *outVx, float *outVy) {
    const int fw = 320;
    int w = prevGray->mat.cols, h = prevGray->mat.rows;
    int fh = std::max(1, (int)std::lround((double)fw * h / w));
    cv::Mat a, b, flow;
    cv::resize(prevGray->mat, a, cv::Size(fw, fh), 0, 0, cv::INTER_AREA);
    cv::resize(gray->mat, b, cv::Size(fw, fh), 0, 0, cv::INTER_AREA);
    cv::calcOpticalFlowFarneback(a, b, flow, 0.5, 3, 15, 3, 5, 1.2, 0);

    // Subtract the per-frame median flow: camera pans/shake move every cell
    // alike and must not count as stroke rhythm.
    std::vector<cv::Mat> ch(2);
    cv::split(flow, ch);
    double scale = (double)w / fw;
    for (int c = 0; c < 2; c++) {
        std::vector<float> v(ch[c].begin<float>(), ch[c].end<float>());
        std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
        float med = v[v.size() / 2];
        cv::Mat cells;
        cv::resize(ch[c] - med, cells, cv::Size(gw, gh), 0, 0, cv::INTER_AREA);
        float *out = c == 0 ? outVx : outVy;
        for (int y = 0; y < gh; y++)
            for (int x = 0; x < gw; x++)
                out[y * gw + x] = (float)(cells.at<float>(y, x) * scale);
    }
}

TrackerHandle Tracker_Create(void) {
    Tracker_ *t = new Tracker_();
    t->tracker = cv::TrackerCSRT::create();
    return t;
}

void Tracker_Close(TrackerHandle t) { delete t; }

void Tracker_Init(TrackerHandle t, VideoCapture v, int x, int y, int w, int h) {
    cv::Rect box(x, y, w, h);
    t->tracker->init(v->frame, box);
}

int Tracker_Update(TrackerHandle t, VideoCapture v, int *x, int *y, int *w, int *h) {
    cv::Rect box;
    bool ok = t->tracker->update(v->frame, box);
    *x = box.x;
    *y = box.y;
    *w = box.width;
    *h = box.height;
    return ok ? 1 : 0;
}

}
