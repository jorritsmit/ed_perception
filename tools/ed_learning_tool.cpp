/*
* Author: Luis Fererira
* E-mail: luisfferreira@outlook.com
* Date: January 2015
*/

// ed includes
#include <ed/perception/module.h>

#include <ed/entity.h>
#include <ed/world_model.h>
#include <ed/update_request.h>

// Measurement data structures
#include <ed/measurement.h>
#include <rgbd/Image.h>

// Include the perception plugin
#include "../src/perception_plugin.h"

// File crawling
#include <tue/filesystem/crawler.h>

// Model loading
#include <ros/package.h>

// Measurement loading
#include <ed/io/filesystem/read.h>
#include <fstream>
#include <rgbd/serialization.h>
#include <ed/serialization/serialization.h>

// Show measurement
#include <opencv2/highgui/highgui.hpp>

// odu_finder
#include "odu_finder_db_builder.cpp"

// RGBD
#include <rgbd/Image.h>
#include <rgbd/View.h>

// file writting
#include <fstream>
#include <string>
#include <iostream>

#include <boost/filesystem.hpp>

#include <ros/package.h>

std::string module_name_;

// ----------------------------------------------------------------------------------------------------

void showMeasurement(const ed::Measurement& msr)
{
    const cv::Mat& rgb_image = msr.image()->getRGBImage();
    const cv::Mat& depth_image = msr.image()->getDepthImage();

    cv::Mat masked_rgb_image(rgb_image.rows, rgb_image.cols, CV_8UC3, cv::Scalar(0, 0, 0));
    cv::Mat masked_depth_image(depth_image.rows, depth_image.cols, depth_image.type(), 0.0);

    for(ed::ImageMask::const_iterator it = msr.imageMask().begin(rgb_image.cols); it != msr.imageMask().end(); ++it)
        masked_rgb_image.at<cv::Vec3b>(*it) = rgb_image.at<cv::Vec3b>(*it);

    for(ed::ImageMask::const_iterator it = msr.imageMask().begin(depth_image.cols); it != msr.imageMask().end(); ++it)
        masked_depth_image.at<float>(*it) = depth_image.at<float>(*it);

    cv::imshow("Measurement: depth", masked_depth_image / 8);
    cv::imshow("Measurement: rgb", masked_rgb_image);

    cv::waitKey();
}

// ----------------------------------------------------------------------------------------------------

void config_to_file(tue::Configuration& config, const std::string &model_name, const std::string &save_directory){

    std::string file_dir = save_directory + "/" + model_name + "/" + model_name + ".yml";

    try {
        boost::filesystem::path dir(save_directory + "/" + model_name);
        boost::filesystem::create_directories(dir);
    } catch(const boost::filesystem::filesystem_error& e){
       if(e.code() == boost::system::errc::permission_denied)
           std::cout << "boost::filesystem permission denied" << std::endl;
       else
           std::cout << "boost::filesystem failed with error: " << e.code().message() << std::endl;
    }

    std::cout << "[" << module_name_ << "] " << "Saving model for '" << model_name << "' at " << file_dir << std::endl;

    std::ofstream out(file_dir.c_str(), std::ofstream::out);
    if (out.is_open()){
        out << config.toYAMLString();
        out.close();
    }else
        std::cout << "[" << module_name_ << "]" << "Could not create file" << std::endl;
}


// ----------------------------------------------------------------------------------------------------

void parse_config(tue::Configuration& config, const std::string &module_name, const std::string &model_name, tue::Configuration& final_config){


    // --------------- PARSE INFORMATION ---------------

    // step into perception_result group
    if (!config.readGroup("perception_result")){
        std::cout << "[" << module_name_ << "] " << "Could not find the perception_result group" << std::endl;
        return;
    }

    // step into the group being parsed
    if (!config.readGroup(module_name)){
        std::cout << "[" << module_name_ << "] " << "Could not find the " << module_name << " group" << std::endl;
        config.endGroup(); // close type_aggregator group in case this one fails
        return;
    }

    float height = 0;
    float width = 0;
    float area = 0;
    bool read_size = false;
    bool read_color = false;
    std::string color_name;
    float amount;
    std::map<std::string, float> color_info;

    // parse information from current config
    if(module_name.compare("size_matcher") == 0){       // PARSE SIZE MATCHER
        if (config.readGroup("size")){
            if (config.value("height", height) && config.value("width", width) && config.value("area", area)){
//                std::cout << "[" << module_name_ << "] " << "Read HxW " << height << " x " << width << " x " << area << std::endl;
                read_size = true;
            }else{
                std::cout << "[" << module_name_ << "] " << "'size_matcher' group incorrectly built" << std::endl;
            }
            config.endGroup(); // close size group
        }
    }else if(module_name.compare("color_matcher") == 0){    // PARSE COLOR MATCHER
        if (config.readArray("colors")){
            while(config.nextArrayItem()){
                if (config.value("name", color_name) && config.value("value", amount)){
                    color_info[color_name] = amount;
                    read_color = true;
                }
            }
            config.endArray(); // close hypothesis array
        }else{
            std::cout << "[" << module_name_ << "] " << "'color_matcher' group incorrectly built" << std::endl;
        }
    }

    config.endGroup(); // close the parsed group
    config.endGroup(); // close perception_result group


    // --------------- WRITE PARSED INFORMATION ---------------


    // create or read perception_result group
    if (!final_config.readGroup("model", tue::OPTIONAL)){
        final_config.writeGroup("model");
    }

    final_config.setValue("name", model_name);

    // save size information
    if (read_size){
        if (!final_config.readArray("size", tue::OPTIONAL)){
            final_config.writeArray("size");
        }

        final_config.addArrayItem();
        final_config.setValue("height", height);
        final_config.setValue("width", width);
        final_config.setValue("area", area);
        final_config.endArrayItem();

        final_config.endArray();
    }

    // save color information
    if(read_color){
        if (!final_config.readArray("color", tue::OPTIONAL)){
            final_config.writeArray("color");
        }

        final_config.addArrayItem();
        final_config.writeArray("set");

        for(std::map<std::string, float>::const_iterator color_it = color_info.begin(); color_it != color_info.end(); ++color_it) {
            final_config.addArrayItem();
            final_config.setValue(color_it->first, color_it->second);
            final_config.endArrayItem();
        }

        final_config.endArray();    // close set array
        final_config.endArrayItem();// close array item
        final_config.endArray();    // close color array
    }

    final_config.endGroup(); // close model group
}


// ----------------------------------------------------------------------------------------------------


void imageToOduFinder(ed::EntityConstPtr& entity, OduDBBuilder& odu_learner, std::string model_name){
    // ---------- PREPARE MEASUREMENT ----------

    // Get the best measurement from the entity
    ed::MeasurementConstPtr msr = entity->lastMeasurement();
    if (!msr)
        return;

    uint min_x, max_x, min_y, max_y;

    // create a view
    rgbd::View view(*msr->image(), msr->image()->getRGBImage().cols);

    // get color image
    const cv::Mat& color_image = msr->image()->getRGBImage();

    // crop it to match the view
    cv::Mat cropped_image(color_image(cv::Rect(0,0,view.getWidth(), view.getHeight())));

    // initialize bounding box points
    max_x = 0;
    max_y = 0;
    min_x = view.getWidth();
    min_y = view.getHeight();

    // initialize mask
    cv::Mat mask = cv::Mat::zeros(view.getHeight(), view.getWidth(), CV_8UC1);
    // Iterate over all points in the mask
    for(ed::ImageMask::const_iterator it = msr->imageMask().begin(view.getWidth()); it != msr->imageMask().end(); ++it)
    {
        // mask's (x, y) coordinate in the depth image
        const cv::Point2i& p_2d = *it;

        // paint a mask
        mask.at<unsigned char>(*it) = 255;

        // update the boundary coordinates
        if (min_x > p_2d.x) min_x = p_2d.x;
        if (max_x < p_2d.x) max_x = p_2d.x;
        if (min_y > p_2d.y) min_y = p_2d.y;
        if (max_y < p_2d.y) max_y = p_2d.y;
    }

//    optimizeContourBlur(mask, mask);

    // ---------- LEARN MEASUREMENT ----------

    // crop, convert to grayscale and increase contrast of the image
    cv::Mat roi(cropped_image(cv::Rect(min_x, min_y, max_x - min_x, max_y - min_y)));
    cv::cvtColor(roi, roi, CV_BGR2GRAY);
    cv::equalizeHist(roi , roi);

    odu_learner.learnImage(model_name + "-" + ed::Entity::generateID().str(), roi);
}

// ----------------------------------------------------------------------------------------------------

bool loadModelList(std::string& model_list_path, std::vector<std::string>& model_list){
    tue::Configuration conf;
    std::string model_name;

    if (conf.loadFromYAMLFile(model_list_path)){    // read YAML configuration
        if (conf.readArray("models")){              // read Model group

            while(conf.nextArrayItem()){
                if(conf.value("name", model_name))
                    model_list.push_back(model_name);
            }

            conf.endArray();    // close Models group
        }else{
            std::cout << "[" << module_name_ << "] " << "Could not find 'models' group" << std::endl;
            return false;
        }
    }else{
//        std::cout << "[" << "perception_module" << "] " << "Could not load YML file." << std::endl;
        return false;
    }

    std::cout << "[" << module_name_ << "] " << "Model names in the list: ";
    for(std::vector<std::string>::const_iterator i = model_list.begin(); i != model_list.end(); ++i)
        std::cout << *i << ", ";
    std::cout << std::endl;

    return true;
}


// ----------------------------------------------------------------------------------------------------


int main(int argc, char **argv)
{
    std::string measurement_dir;
    std::string model_output_dir;
    std::string db_output_dir;
    std::string model_list_path;
    std::string config_filename;

    if (argc == 6)
    {
        measurement_dir = argv[1];
        model_list_path = argv[2];
        model_output_dir = argv[3];
        db_output_dir = argv[4];
        config_filename = argv[5];

    }else if (argc == 1){
        // if specific paths are not specified, use ROS get path
        std::string ed_models_dir = ros::package::getPath("ed_object_models");

        measurement_dir = ed_models_dir + "/models";
        model_list_path = ed_models_dir + "/configs/model_lists/all_models.yml";
        model_output_dir = ed_models_dir + "/models";
        db_output_dir = ed_models_dir + "/configs/odu_finder";
    }else{
        std::cout << "Usage for:\n\n   ed-learning-tool MEASUREMENTS_DIRECTORY MODEL_LIST MODEL_LEARNING_DIRECTORY ODU_DB_DIRECTORY \n\n" << std::endl;
        std::cout << "\tMEASUREMENT_DIRECTORY - directory with the measurements separated in sub-folders. Sub-folder name will be used as model name" << std::endl;
        std::cout << "\tMODEL_LIST - List of models to be learned, from the available in the measurements directory (YML file)" << std::endl;
        std::cout << "\tMODEL_LEARNING_DIRECTORY - directory where the model learning files will be stored" << std::endl;
        std::cout << "\tODU_DB_DIRECTORY - directory where the ODU Finder database will be stored" << std::endl;
        std::cout << "\n" << std::endl;
        return 1;
    }

    std::vector<std::string> model_list;
    module_name_ = "ed_learning_tool";

    if (loadModelList(model_list_path, model_list))
        std::cout << "[" << module_name_ << "] " << "Model list loaded." << std::endl;
    else
        std::cout << "[" << module_name_ << "] " << "Could not load model list from " << model_list_path << std::endl;


    // ---------------- LOAD PERCEPTION LIBRARIES ----------------

    // load this one separately
    OduDBBuilder odu_learner = OduDBBuilder(db_output_dir + "odu_debug/");

    ed::perception::PerceptionPlugin plugin;

    // Needed to configure the plugin
    ed::PropertyKeyDB ed_property_key_db;

    tue::Configuration config;
    config.loadFromYAMLFile(config_filename);

    if (config.hasError())
    {
        std::cout << std::endl << "Error during configuration:" << std::endl << std::endl << config.error() << std::endl;
        return 1;
    }

    if (config.readArray("plugins", tue::REQUIRED))
    {
        while(config.nextArrayItem())
        {
            std::string plugin_name, plugin_lib;
            if (!config.value("name", plugin_name) || !config.value("lib", plugin_lib) || plugin_name != "perception")
                continue;

            if (config.readGroup("parameters", tue::REQUIRED))
            {
                ed::InitData init(ed_property_key_db, config);
                plugin.initialize(init);

                config.endGroup();
            }
        }

        config.endArray();
    }

    if (config.hasError())
    {
        std::cout << std::endl << "Error during configuration:" << std::endl << std::endl << config.error() << std::endl;
        return 1;
    }

    // ---------------- CRAWL THROUGH MEASUREMENTS ----------------

    tue::filesystem::Crawler crawler(measurement_dir);

    std::set<std::string> files_had;

    int n_measurements = 0;
    tue::filesystem::Path filename;
    tue::Configuration parsed_conf;
    std::string model_name;
    std::string last_model = "";
    bool first_model = true;

    while(crawler.nextPath(filename))
    {
        std::string filename_without_ext = filename.withoutExtension().string();
        if (files_had.find(filename_without_ext) != files_had.end())
            continue;

        files_had.insert(filename_without_ext);

        ed::EntityConstPtr e;
        ed::WorldModel wm;

        if (tue::filesystem::Path(filename_without_ext + ".json").exists())
        {
            ed::UpdateRequest update_req;
            if (!ed::readEntity(filename_without_ext + ".json", update_req))
                continue;

            wm.update(update_req);

            if (wm.numEntities() == 0)
                continue;

            e = *wm.begin();
        }
        else if (tue::filesystem::Path(filename_without_ext + ".mask").exists())
        {
            ed::MeasurementPtr msr(new ed::Measurement);
            if (!ed::read(filename_without_ext, *msr))
                continue;

            ed::EntityPtr e_temp(new ed::Entity("test-entity", "", 5));
            e_temp->addMeasurement(msr);

            e = e_temp;
        }

        // get info on model name from path
        model_name = filename_without_ext.substr(0, filename_without_ext.find_last_of("/"));    // remove measurement ID
        model_name = model_name.substr(model_name.find_last_of("/")+1);     // get parent folder name / model name

        // skip model if its not on the model list
        if (!(std::find(model_list.begin(), model_list.end(), model_name) != model_list.end() || model_list.empty())){
            std::cout << "Skipping model measurements '" << model_name << "', not on the list" << std::endl;
            model_name = last_model;
            continue;
        }

        // get the name of the first model, when found
        if(first_model){
            last_model = model_name;
            first_model = false;
        }

        // if the model name changes, save the learned information
        if (last_model.compare(model_name) != 0){
            config_to_file(parsed_conf, last_model, model_output_dir);
            // reset config for new model
            parsed_conf = tue::Configuration();
            last_model = model_name;
        }

        std::cout << "Processing model: " << model_name << std::endl;

        if (!e)
            continue;

//        if (e->lastMeasurement())
//            showMeasurement(*e->lastMeasurement());

        ed::perception::WorkerInput input;
        input.entity = e;

        ed::perception::WorkerOutput output;
        tue::Configuration result;
        output.data = result;

        // ---------------- PROCESS CURRENT MEASUREMENT ----------------

        const std::vector<boost::shared_ptr<ed::perception::Module> >& modules = plugin.perception_modules();

        for(std::vector<boost::shared_ptr<ed::perception::Module> >::const_iterator it = modules.begin(); it != modules.end(); ++it)
        {
            const boost::shared_ptr<ed::perception::Module>& module = *it;

            module->process(input, output);

            parse_config(result, module->name(), model_name, parsed_conf);

//            std::cout << "Parsed " << module->name() << ": \n" << parsed_conf << std::endl;
        }

        // print perception result
//        std::cout << result << std::endl;

        // send the object image to the OduFinder database
        imageToOduFinder(e, odu_learner, model_name);

        ++n_measurements;
    }

    if (n_measurements == 0)
        std::cout << "No measurements found." << std::endl;
    else{
        // save last parsed model
        config_to_file(parsed_conf, model_name, model_output_dir);
        // compile Odu Finder database
        odu_learner.buildDatabase(db_output_dir + "database/");
    }

    return 0;
}

