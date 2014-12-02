#include "color_matcher.h"

#include "ed/measurement.h"
#include <ed/entity.h>

#include <rgbd/Image.h>
#include <rgbd/View.h>


ColorMatcher::ColorMatcher() :
    PerceptionModule("color_matcher"),
    color_table_(ColorNameTable::instance()),  // Init colorname table
    init_success_(false)
{
}

// ---------------------------------------------------------------------------------------------------

ColorMatcher::~ColorMatcher()
{
}

// ----------------------------------------------------------------------------------------------------

void ColorMatcher::loadConfig(const std::string& config_path) {

    kModuleName = "color_matcher";
    kDebugFolder = "/tmp/color_matcher/";
    kDebugMode = false;

    if (kDebugMode)
        CleanDebugFolder(kDebugFolder);

    std::cout << "[" << kModuleName << "] " << "Loading color names..." << std::endl;

    if (!color_table_.load_config(config_path + "/color_names.txt")){
        std::cout << "[" << kModuleName << "] " << "Failed loading color names from " +config_path + "/color_names.txt" << std::endl;
        return;
    }

    init_success_ = true;
    std::cout << "[" << kModuleName << "] " << "Ready!" << std::endl;
}


// ---------------------------------------------------------------------------------------------------


void ColorMatcher::loadModel(const std::string& model_name, const std::string& model_path)
{
    std::string models_folder = model_path.substr(0, model_path.find_last_of("/") - 1); // remove last slash
    models_folder = models_folder.substr(0, models_folder.find_last_of("/"));   // remove color from path

    std::string path = models_folder + "/models/" + model_name +  "/" +  model_name + ".yml";

    if (load_learning(path, model_name))
        std::cout << "[" << kModuleName << "] " << "Loaded colors for " << model_name << std::endl;
    else{
//        std::cout << "[" << kModuleName << "] " << "Couldn not load colors for " << path << "!" << std::endl;
    }
}


// ---------------------------------------------------------------------------------------------------


void ColorMatcher::process(ed::EntityConstPtr e, tue::Configuration& result) const
{
    if (!init_success_)
        return;

    // ---------- PREPARE MEASUREMENT ----------

    // Get the best measurement from the entity
    ed::MeasurementConstPtr msr = e->lastMeasurement();
    if (!msr)
        return;

    uint min_x, max_x, min_y, max_y;
    std::map<std::string, double> hypothesis;

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

    optimizeContourBlur(mask, mask);

    // ---------- PROCESS MEASUREMENT ----------

    // Calculate img color prob
    cv::Mat roi (cropped_image(cv::Rect(min_x, min_y, max_x - min_x, max_y - min_y)));
    cv::Mat roi_mask (mask(cv::Rect(min_x, min_y, max_x - min_x, max_y - min_y)));

    cv::Mat color_hist;
    std::map<std::string, double> color_prob = getImageColorProbability(roi, roi_mask, color_hist);

//    std::cout << "obj_hist = " << color_hist << std::endl;

    getHypothesis(color_prob, color_hist, hypothesis);

    // ---------- ASSERT RESULTS ----------

    // create group if it doesnt exist
    if (!result.readGroup("perception_result", tue::OPTIONAL))
    {
        result.writeGroup("perception_result");
    }

    result.writeGroup("color_matcher");

    // assert colors
    if (!color_prob.empty()){
        result.writeArray("colors");
        for (std::map<std::string, double>::const_iterator it = color_prob.begin(); it != color_prob.end(); ++it)
        {
            result.addArrayItem();
            result.setValue("name", it->first);
            result.setValue("value", it->second);
            result.endArrayItem();
        }
        result.endArray();
    }


    // assert hypothesis
    if (!hypothesis.empty()){
        result.writeArray("hypothesis");
        for (std::map<std::string, double>::const_iterator it = hypothesis.begin(); it != hypothesis.end(); ++it)
        {
            result.addArrayItem();
            result.setValue("name", it->first);
            result.setValue("score", it->second);
            result.endArrayItem();
        }
        result.endArray();
    }

    result.endGroup();  // close color_matcher group
    result.endGroup();  // close perception_result group


    // ---------- DEBUG ----------

    if (kDebugMode){
        cv::Mat temp;
        std::string id = ed::Entity::generateID();

        roi.copyTo(temp, roi_mask);

        cv::imwrite(kDebugFolder + id + "_color_matcher_full.png", roi);
        cv::imwrite(kDebugFolder + id + "_color_matcher_masked.png", temp);
    }
}

// ---------------------------------------------------------------------------------------------------

std::map<std::string, double> ColorMatcher::getImageColorProbability(const cv::Mat& img, const cv::Mat& mask, cv::Mat& histogram) const
{
    std::map<std::string, unsigned int> color_count;
    uint pixel_count = 0;

    // Loop over the image
    for(int y = 0; y < img.rows; ++y) {
        for(int x = 0; x < img.cols; ++x) {

            // only use the points covered by the mask
            if (mask.at<unsigned char>(y, x) > 0){
                pixel_count ++;

                // Calculate prob distribution
                const cv::Vec3b& c = img.at<cv::Vec3b>(y, x);

                ColorNamePoint cp((float) c[2],(float) c[1],(float) c[0]);
                std::vector<ColorProbability> probs = color_table_.getProbabilities(cp);

                std::string highest_prob_name;
                float highest_prob = 0;

                for (std::vector<ColorProbability>::iterator it = probs.begin(); it != probs.end(); ++it) {

                    if (it->probability() > highest_prob) {
                        highest_prob = it->probability();
                        highest_prob_name = it->name();
                    }
                }

                // Check if the highest prob name exists in the map
                std::map<std::string, unsigned int>::iterator found_it = color_count.find(highest_prob_name);
                if (found_it == color_count.end()) // init on 1
                    color_count.insert( std::pair<std::string, unsigned int>(highest_prob_name,1) );
                else // +1
                    color_count[highest_prob_name] += 1;

                // Set the color in the image (vis purposes only)

                int r,g,b;
                colorToRGB(stringToColor(highest_prob_name),r,g,b);
//                img.at<cv::Vec3b>(y, x) = cv::Vec3b(b,g,r);       // Paint over the image for debugging
            }
        }
    }

    // initialize histogram
    histogram = cv::Mat::zeros(1, ColorNames::getTotalColorsNum(), CV_8UC1);

    std::map<std::string, double> color_prob;
    for (std::map<std::string, unsigned int>::const_iterator it = color_count.begin(); it != color_count.end(); ++it) {
        color_prob.insert(std::pair<std::string, double>(it->first, (double) it->second/pixel_count));

        int i=0;
        // Iterate through all existing colors, Orange is 0, Black is 10, because...
        for (ColorNames::Color it_color = ColorNames::Orange; it_color <= ColorNames::Black; ++it_color, i++)
        {
            if (colorToString(it_color).compare(it->first) == 0){
//                std::cout << "in " << it->first << ", " << i << " - " << it->second << std::endl;
                histogram.at<uchar>(i) = it->second;
            }
        }
    }

    return color_prob;
}

// ----------------------------------------------------------------------------------------------------


void ColorMatcher::getHypothesis(std::map<std::string, double>& color_prob, cv::Mat& curr_hist, std::map<std::string, double>& hypothesis) const
{

    std::map<std::string, std::vector<std::map<std::string, double> > >::const_iterator model_it;
    std::map<std::string, double>::const_iterator set_it;
    std::map<std::string, double>::iterator find_it;
    std::string model_name;
    cv::Mat model_hist;

    double score = 0;
    double best_score;
    double color_amount = 0;

    // iterate through all learned models
    for(model_it = models_colors_.begin(); model_it != models_colors_.end(); ++model_it){
        model_name = model_it->first;
        best_score = std::numeric_limits<double>::max();

        // iterate through all color sets
        for (uint i = 0; i < model_it->second.size(); i++){
            score = 0;
            model_hist = cv::Mat::zeros(1, ColorNames::getTotalColorsNum(), CV_32F);

            // iterate through the colors in this set
            for(set_it = model_it->second[i].begin(); set_it != model_it->second[i].end(); ++set_it){
                int i = 0;

                // find the color amount on the object being tested, if possible
                find_it = color_prob.find(set_it->first);
                if (find_it != color_prob.end()){
                    color_amount = find_it->second;
                }else
                    color_amount = 0;

                score += color_amount * set_it->second;

                // **************************************************
                // **************************************************
                // fill histogram for the current model and set
                for (ColorNames::Color it_color = ColorNames::Orange; it_color <= ColorNames::Black; ++it_color, i++)
                {
                    if (colorToString(it_color).compare(set_it->first) == 0){
                        model_hist.at<uchar>(i) = set_it->second;
                    }
                }
                // **************************************************
                // **************************************************

//                std::cout << "[" << kModuleName << "] " << "Error for " << set_it->first << ": " << color_amount * set_it->second << std::endl;
            }

            // **************************************************
            // **************************************************
            // compare histograms

//            std::cout << "set_hist = " << model_hist << std::endl;

//            std::cout << "Compare hist: " << cv::compareHist(model_hist, curr_hist, CV_COMP_CORREL) << std::endl;
            // **************************************************
            // **************************************************


            if (best_score > score) best_score = score;
        }

        // save hypothesis score, 1 is correct, 0 is incorrect
        hypothesis.insert(std::pair<std::string, double>(model_name, best_score));
    }
}


// ---------------------------------------------------------------------------------------------------

std::string ColorMatcher::getHighestProbColor(std::map<std::string, double>& map) const
{
    double max = 0;
    std::string max_name;
    for (std::map<std::string, double>::const_iterator it = map.begin(); it != map.end(); ++it)
    {
        if (it->second > max) {
            max = it->second;
            max_name = it->first;
        }
    }
    return max_name;
}

// ----------------------------------------------------------------------------------------------------

void ColorMatcher::optimizeContourBlur(const cv::Mat& mask_orig, cv::Mat& mask_optimized) const{

    mask_orig.copyTo(mask_optimized);

    // blur the contour, also expands it a bit
    for (uint i = 6; i < 18; i = i + 2){
        cv::blur(mask_optimized, mask_optimized, cv::Size( i, i ), cv::Point(-1,-1) );
    }

    cv::threshold(mask_optimized, mask_optimized, 50, 255, CV_THRESH_BINARY);
}

// ----------------------------------------------------------------------------------------------------

void ColorMatcher::CleanDebugFolder(const std::string& folder) const{
    if (system(std::string("mkdir " + folder).c_str()) != 0){
        //printf("\nUnable to create output folder. Already created?\n");
    }
    if (system(std::string("rm " + folder + "*.png").c_str()) != 0){
        //printf("\nUnable to clean output folder \n");
    }
}

// ----------------------------------------------------------------------------------------------------

bool ColorMatcher::load_learning(std::string path, std::string model_name){
    if (path.empty()){
        std::cout << "[" << kModuleName << "] " << "Empty path!" << path << std::endl;
        return false;
    }else{
        tue::Configuration conf;
        double amount;
        std::string color;
        std::string model_name = "";

        if (conf.loadFromYAMLFile(path)){       // read YAML configuration
            if (conf.readGroup("model")){       // read Model group
                if (!conf.value("name", model_name)){   // read model Name
                    std::cout << "[" << kModuleName << "] " << "Could not find model name!" << std::endl;
                }
                if (conf.readArray("color")){    // read color array
                    std::vector<std::map<std::string, double> > color_sets;

                    while(conf.nextArrayItem()){
                        if (conf.readArray("set")){    // read set array

                            std::map<std::string, double> set;
                            while(conf.nextArrayItem()){
                                color = "";
                                // Iterate through all existing colors, Orange is 0, Black is 10, because...
                                for (ColorNames::Color it_color = ColorNames::Orange; it_color <= ColorNames::Black; ++it_color)
                                {
                                    // read the color and amount
                                    if (conf.value(colorToString(it_color), amount, tue::OPTIONAL)){
                                        color = colorToString(it_color);
                                        break;
                                    }
                                }

                                if (color.empty())
                                    std::cout << "[" << kModuleName << "] " << "Error: Unmatched color name, not good!" << std::endl;

                                // add color and amount to set
                                set.insert(std::pair<std::string, double>(color, amount));
                            }
                            conf.endArray();     // close Set array

                            // save the set
                            color_sets.push_back(set);
                        }
                    }

                    // save the sets for this model
                    models_colors_.insert(std::pair<std::string, std::vector<std::map<std::string, double> > >(model_name, color_sets));

                    conf.endArray();     // close Color array
                }else
                    std::cout << "[" << kModuleName << "] " << "Could not find 'size' group" << std::endl;

                conf.endGroup();    // close Model group
            }else
                std::cout << "[" << kModuleName << "] " << "Could not find 'model' group" << std::endl;
        }else{
//            std::cout << "[" << kModuleName << "] " << "Didn't find configuration file." << std::endl;
            return false;
        }
    }

    return true;
}

ED_REGISTER_PERCEPTION_MODULE(ColorMatcher)