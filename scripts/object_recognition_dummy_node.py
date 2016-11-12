#!/usr/bin/env python

# System
import os
import operator
import sys

# ROS
import rospy

# OpenCV
from cv_bridge import CvBridge, CvBridgeError
import cv2

# Tensorflow
import tensorflow as tf
import numpy as np
from random import random

# TU/e Robotics
from image_recognition_msgs.srv import Recognize
from image_recognition_msgs.msg import Recognition, CategoryProbability
from image_recognition_util import image_writer


class ObjectRecognitionDummyROS:
    def __init__(self, labels_path):
        self._recognize_srv = rospy.Service('recognize', Recognize, self._recognize_srv_callback)

        rospy.loginfo("ObjectRecognitionDummyROS initialized:")
        rospy.loginfo(" - labels_path=%s", labels_path)

        with open(labels_path, "r") as labels_file:
            labels_str = labels_file.read()
            self._labels = labels_str.strip().split("\n")

        rospy.loginfo("Labels = %s", self._labels)

    def _recognize_srv_callback(self, req):
        recognition = Recognition()
        recognition.roi.height = req.image.height
        recognition.roi.width = req.image.width
        recognition.categorical_distribution.unknown_probability = 0.1  # TODO: How do we know this?
        for label in self._labels:
            category_probabilty = CategoryProbability(label=label, probability=random())
            recognition.categorical_distribution.probabilities.append(category_probabilty)

        return {"recognitions": [recognition]}

if __name__ == '__main__':

    # Start ROS node
    rospy.init_node('object_recognition_dummy_ros')

    try:
        _labels_path = os.path.expanduser(rospy.get_param("~labels_path"))
    except KeyError as e:
        rospy.logerr("Parameter %s not found" % e)
        sys.exit(1)

    # Create object
    object_recognition = ObjectRecognitionDummyROS(labels_path=_labels_path)
    rospy.spin()
