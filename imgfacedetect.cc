/* ============================================================
 *
 * This was a part of the face detection of digiKam
 *
 * SPDX-FileCopyrightText: 2019 by Thanh Trung Dinh <dinhthanhtrung1996 at gmail dot com>
 * SPDX-FileCopyrightText: 2020-2022 by Gilles Caulier <caulier dot gilles at gmail dot com>
 * SPDX-FileCopyrightText: 2023 Lars Lindqvist
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * g++ -fPIC img-face-detect.cc `pkg-config --cflags opencv4`  -lopencv_dnn -lopencv_imgcodecs -lopencv_imgproc -lopencv_core 
 *
 * ============================================================ */


#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <err.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fstream>
#include <vector>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/dnn.hpp>

#include <_optparse.h>

static const char progname[] = "imgfacedetect";
static const char default_mpath[] = "digikam/facesengine/deploy.prototxt";
static const char default_dpath[] = "digikam/facesengine/res10_300x300_ssd_iter_140000_fp16.caffemodel";
static const int SSDSIZ = 300;
static const cv::Size SSD_IMGSIZ(300, 300);
static cv::dnn::Net net;
static double confidenceThreshold = 0.7F;
static double nmsThreshold = 0.4F;
static bool verbose = false;

static std::vector<uint8_t>
read_file(const char *path) {
	struct stat st;
	std::vector<uint8_t> buf;

	if (stat(path, &st) < 0) {
		warn("stat %s", path);
		return buf;
	}

	std::ifstream fp(path, std::ios::binary);
	buf.reserve(st.st_size);
	buf.assign(std::istreambuf_iterator<char>(fp),
	           std::istreambuf_iterator<char>());

	return buf;
}

static void
handle(const char *path) {
	std::vector<uint8_t> buf;
	buf = read_file(path);

	cv::Mat img = cv::imdecode(buf, cv::IMREAD_COLOR);
	const int img_w = img.cols;
	const int img_h = img.rows;
	double wf = (double)SSDSIZ / img_w;
	double hf = (double)SSDSIZ / img_h;
	double scalefactor = wf < hf ? wf : hf;
	int nw = img.cols * scalefactor;
	int nh = img.rows * scalefactor;
	cv::resize(img, img, cv::Size(nw, nh));
	int pad_r = SSDSIZ - nw;
	int pad_b = SSDSIZ - nh;
	cv::Mat padimg;
	cv::copyMakeBorder(img, padimg, 0, pad_b, 0, pad_r, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));

	if (padimg.empty())
		return ;

	net.setInput(
	 cv::dnn::blobFromImage(padimg, 1.0, cv::Size(SSDSIZ, SSDSIZ), cv::Scalar(104.0, 177.0, 123.0), true, false)
	);
	cv::Mat detection = net.forward();

	std::vector<float> confidences;
	std::vector<cv::Rect> boxes;

	cv::Mat detectionMat(detection.size[2], detection.size[3], CV_32F, detection.ptr<float> ());

	for (int i = 0; i < detectionMat.rows; ++i) {
		float confidence = detectionMat.at <float>(i, 2);

		if (confidence > confidenceThreshold) {
			double x = detectionMat.at<float> (i, 3) * SSDSIZ;
			double w = detectionMat.at<float> (i, 5) * SSDSIZ - x;
			double y = detectionMat.at<float> (i, 4) * SSDSIZ;
			double h = detectionMat.at<float> (i, 6) * SSDSIZ - y;

			int bR = SSDSIZ - pad_r;
			int bB = SSDSIZ - pad_b;

			if ((x     >= cv::min(0.0,                  -0.1 * w)) &&
			    (x + w <= cv::max(bR + 0.1 * pad_r, bR + 0.1 * w)) &&
			    (y     >= cv::min(0.0,                  -0.1 * h)) &&
			    (y + h <= cv::max(bB + 0.1 * pad_b, bB + 0.1 * h))) {
				boxes.push_back(cv::Rect(x, y, w, h));
				confidences.push_back(confidence);
			}
		}
	}

	// Perform non maximum suppression to eliminate redundant overlapping boxes with lower confidences

	std::vector<int> indices;
	cv::dnn::NMSBoxes(boxes, confidences, confidenceThreshold, nmsThreshold, indices);

	// Get detected bounding boxes

	for (size_t i = 0; i < indices.size(); ++i) {
		cv::Rect bbox = boxes[indices[i]];
		int x = cv::max(0.0, bbox.x / scalefactor);
		int y = cv::max(0.0, bbox.y / scalefactor);
		int w = cv::min((double)img_w, bbox.width  / scalefactor + x) - x;
		int h = cv::min((double)img_h, bbox.height / scalefactor + y) - y;

		printf("%d %d %d %d\t%s\n", x, y, w, h, path);
	}
}

static const struct optparse_long longopts[] = {
	{ "verbose",        'v', OPTPARSE_NONE },
	{ "quiet",          'q', OPTPARSE_NONE },
	{ "score-threshold",'T', OPTPARSE_REQUIRED },
	{ "NMS-threshold",  'N', OPTPARSE_REQUIRED },
	{ "mpath",          'm', OPTPARSE_REQUIRED },
	{ "dpath",          'd', OPTPARSE_REQUIRED },

	{ "zsh-comp-gen", -3515, OPTPARSE_NONE },
	{ "help", 'h', OPTPARSE_NONE },
	{ 0 },
};

static void
usage() {
	printf("usage: %s [opts] <file ...>\n", __FILE__);
	printf(" -T <score threshold> (%.2f)\n", confidenceThreshold);
	printf(" -N <NMS threshold> (%.2f)\n", nmsThreshold),
	printf(" -d <dpath> (%s)\n", default_dpath);
	printf(" -m <mpath> (%s)\n", default_mpath);
	exit(1);
}

int
main(int argc, char *argv[]) {
	char _dpath[PATH_MAX];
	char _mpath[PATH_MAX];

	const char *xdg = getenv("XDG_DATA_HOME");
	snprintf(_dpath, sizeof(_dpath) - 1, "%s/%s", xdg ? xdg : ".", default_dpath);
	snprintf(_mpath, sizeof(_mpath) - 1, "%s/%s", xdg ? xdg : ".", default_mpath);

	char *mpath = _mpath;
	char *dpath = _dpath;

	struct optparse op;
	long opt;

	optparse_init(&op, argv);
	while ((opt = optparse_long(&op, longopts, NULL)) != -1) {
		switch (opt) {
		case 'v':
			verbose = true;
			break;
		case 'q':
			verbose = false;
			break;

		case 'd':
			dpath = op.optarg;
			break;
		case 'm':
			mpath = op.optarg;
			break;
		case 'T':
			confidenceThreshold = atof(op.optarg);
			break;
		case 'N':
			nmsThreshold = atof(op.optarg);
			break;

		case '?':
			warnx("%s", op.errmsg);
			usage();
			break;
		case -3515:
			optparse_dump_zsh_comp(longopts, progname, "_files");
			exit(0);
		}
	}

	argv += op.optind;
	argc -= op.optind;

	if (!argc)
		usage();

	try {
		net = cv::dnn::readNetFromCaffe(mpath, dpath);
		for (int i = 0; i < argc; ++i) {
			handle(argv[i]);
		}
	} catch (cv::Exception& e) {
		errx(1, "%s", e.what());
	}

	return 0;
}
