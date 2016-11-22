/*
 *  Copyright (c) 2015, Nagoya University
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither the name of Autoware nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 *  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 *  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 *  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 *  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <cstdlib>
#include <cstdint>
#include <iostream>

#include <opencv/cv.h>
#include <opencv/highgui.h>

#include <ros/ros.h>
#include <sensor_msgs/image_encodings.h>
#include <sensor_msgs/Image.h>
#include <dynamic_reconfigure/server.h>
#include <fake_drivers/FakeCameraConfig.h>

static void update_img_msg(std::string image_file, IplImage** img_p, sensor_msgs::Image& msg)
{
	if (image_file.empty()) {
		return;
	}
	std::cerr << "Image='" << image_file << "'" << std::endl;
	IplImage* img = cvLoadImage(image_file.c_str(), CV_LOAD_IMAGE_ANYDEPTH | CV_LOAD_IMAGE_ANYCOLOR);
	if (img == nullptr) {
		std::cerr << "Can't load " << image_file << "'" << std::endl;
		return;
	}
	if (*img_p != nullptr) {
		cvReleaseImage(img_p);
	}
	*img_p = img;

	msg.width = img->width;
	msg.height = img->height;
	msg.is_bigendian = 0;
	msg.step = img->widthStep;

	uint8_t *data_ptr = reinterpret_cast<uint8_t*>(img->imageData);
	std::vector<uint8_t> data(data_ptr, data_ptr + img->imageSize);
	msg.data = data;

	msg.encoding = (img->nChannels == 1) ? 
		sensor_msgs::image_encodings::MONO8 : 
		sensor_msgs::image_encodings::RGB8;
}

static bool is_1st(uint32_t level)
{
	return level == 0xffffffff;
}

static uint32_t get_level(fake_drivers::FakeCameraConfig &config, std::string name)
{
	const std::vector<fake_drivers::FakeCameraConfig::AbstractParamDescriptionConstPtr> &
		params = config.__getParamDescriptions__();

	for (std::vector<fake_drivers::FakeCameraConfig::AbstractParamDescriptionConstPtr>::const_iterator
		_i = params.begin(); _i != params.end(); ++_i) {
		if (name == (*_i)->name) {
			return (*_i)->level;
		}
	}
	return 0;
}

static bool is_update(fake_drivers::FakeCameraConfig &config, uint32_t level, std::string name)
{
	uint32_t name_level = get_level(config, name);
	return name_level ? (level & name_level) != 0 : false;
}

static void callback(fake_drivers::FakeCameraConfig &config, uint32_t level,
		     IplImage** img_p, sensor_msgs::Image& msg, ros::Rate** rate_p)
{
	if (is_1st(level)) {
		std::cerr << "1st" << std::endl;
	}
	std::cerr << "level=" << level 
		<< " image_file=" << config.image_file
		<< " fps=" << config.fps << std::endl;

	if (is_update(config, level, "image_file")) {
		update_img_msg(config.image_file, img_p, msg);
		std::cerr << " update image_file" << std::endl;
	}
	if (is_update(config, level, "fps")) {
		if (*rate_p != nullptr) {
			delete *rate_p;
		}
		*rate_p = new ros::Rate(config.fps);
		std::cerr << " update fps" << std::endl;
	}
}

int main(int argc, char **argv)
{
	ros::init(argc, argv, "fake_camera");
	ros::NodeHandle n;

	if (argc < 2) {
		std::cerr << "Usage: fake_driver image_file" << std::endl;
		std::exit(1);
	}
	
	IplImage* img = nullptr;
	sensor_msgs::Image msg;
	update_img_msg(argv[1], &img, msg);

	ros::Rate* rate = new ros::Rate(30);

	dynamic_reconfigure::Server<fake_drivers::FakeCameraConfig> server;
	dynamic_reconfigure::Server<fake_drivers::FakeCameraConfig>::CallbackType f;
	f = boost::bind(&callback, _1, _2, &img, msg, &rate);
	server.setCallback(f);

	ros::Publisher pub = n.advertise<sensor_msgs::Image>("image_raw", 1000);

	uint32_t count = 0;
	while (ros::ok()) {
		msg.header.seq = count;
		msg.header.frame_id = count;
		msg.header.stamp.sec = ros::Time::now().toSec();
		msg.header.stamp.nsec = ros::Time::now().toNSec();
		pub.publish(msg);
		ros::spinOnce();
		rate->sleep();
		count++;
	}
	if (img != nullptr) {
		cvReleaseImage(&img);
	}
	return 0;
}
