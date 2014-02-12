#include "ros/ros.h"
#include "std_msgs/String.h"
#include "std_msgs/Int16.h"
#include "std_msgs/Int32MultiArray.h"
#include "std_msgs/Empty.h"

#include <exception>
#include <iostream>
#include <vector>
#include <GClasses/GApp.h>
#include <GClasses/GError.h>
#include <GClasses/GRand.h>
#include <GClasses/GDecisionTree.h>

/**** Node responsible for whackamole learning. */

using namespace GClasses;
using std::cerr;
using std::cout;
using std::vector;

// Flag indicating whether a game has started
static bool gameStarted;
// Flag indicating whether or not autonomous execution mode is enabled.
static bool autonomousMode;
// Flag indicating whether or not a new mole state has been received.
static bool newState;
// Flag indicating whether a new action can now be taken.
static bool newActionAllowed;
// Flag indicating whether the decision tree has been initialized.
static bool treeInitialized;

// Array of most recent mole state values.
static int moleStates[7];

// Robot base and arm position values
static int armPos;
static int robotPos;

// Creates the decision tree used in determining autonomous whackamole behavior.
void createDecisionTree(GDecisionTree &dtree)
{
  //mole0,mole1,mole2,mole3,mole4,mole5,mole6,robotPos,armPos
// Define the feature attributes (or columns)
	vector<size_t> feature_values;
	int i=0;
	for (i=0; i<7; i++) feature_values.push_back(0); //7 continuous moles
	feature_values.push_back(3); // armPos={0=left,1=mid,2=right}
	feature_values.push_back(3); // robotPos={0=left,1=mid,2=right}

	// Define the label attributes (or columns)
	vector<size_t> label_values;
	label_values.push_back(6); // action: {0-2=armPos, 3-5=robotPos}

	// Load the state and action data into GMatrix objects
	GMatrix features(feature_values); // states
	GMatrix labels(label_values); // actions
	
	features.loadCsv("/home/andywolff/ros-groovy/src/whackamole_learning/src/states.csv",',');
	
	labels.loadCsv("/home/andywolff/ros-groovy/src/whackamole_learning/src/actions.csv",',');
	
	// Train the decision tree based on the data
	dtree.train(features, labels);
	//dtree.print(cout);
	//dtree.predict(test_features, predicted_labels);
	
}

void initializeTree (GDecisionTree &decisionTree)
{
  ROS_INFO("Attempting to create decision tree");

	int nRet = 0;
	try
	{
		createDecisionTree(decisionTree);
		treeInitialized = 1;

                // Labels to make printing fancier
                GArffRelation featureRelation;
                featureRelation.addAttribute("mole0", 0, NULL);
                featureRelation.addAttribute("mole1", 0, NULL);
                featureRelation.addAttribute("mole2", 0, NULL);
                featureRelation.addAttribute("mole3", 0, NULL);
                featureRelation.addAttribute("mole4", 0, NULL);
                featureRelation.addAttribute("mole5", 0, NULL);
                featureRelation.addAttribute("mole6", 0, NULL);
                //std::vector<const char *> robotPosVector {"left", "middle", "right"};
                featureRelation.addAttribute("robotPos", 3, NULL);
                //std::vector<const char *> armPosVector {"left", "middle", "right"};
                featureRelation.addAttribute("armPos", 3, NULL);

                decisionTree.print(cout);//, &featureRelation, NULL);
	}
	catch(const std::exception& e)
	{
	  cerr << "ERROR" << "\n";
		cerr << e.what() << "\n";
		nRet = 1;
	}


  ROS_INFO("Created decision tree");

}

/**
 * Callback function called after a mole has been successfully whacked.
 */
void whackCallback(const std_msgs::Int16::ConstPtr& msg)
{
  // Update arm position based on robot position
  int moleWhacked = msg->data;
  armPos = moleWhacked - 2*robotPos;
  // Action complete. Allow a new action to be selected.
  newActionAllowed = 1;
}

/**
 * Callback function called after the robot base has finished moving to
 * a new position.
 */
void robotArriveCallback(const std_msgs::Int16::ConstPtr& msg)
{
  // Update robot position
  robotPos = msg->data - 1; // message value is 1-indexed
  // Action complete. Allow a new action to be selected.
  newActionAllowed = 1;
}

/**
 * Callback function called when mole state data is received.
 */
void stateCallback(const std_msgs::Int32MultiArray::ConstPtr& msg)
{
  // Sets the mole state values equal to the new values received in the message.
  int i = 0;
  for(std::vector<int>::const_iterator iter = msg->data.begin(); iter != msg->data.end(); iter++) {
    moleStates[i] = *iter;
    i++;
  }
  newState = 1;
}

/**
 * Callback function for enabling or disabling autonomous mode.
 */
void autoModeCallback(const std_msgs::Int16::ConstPtr& msg)
{
  autonomousMode = msg->data; // 1=enabled, 0=disabled
}

/**
 * Callback function called when game is started.
 */
void gameStartedCallback(const std_msgs::Empty::ConstPtr& msg)
{
  gameStarted = 1;
}

/**
 * Callback function called when time left in the game changes.
 */
void timeLeftCallback(const std_msgs::Int16::ConstPtr& msg)
{
  int timeLeft = msg->data;
  if(timeLeft <= 0) {
    gameStarted = 0;
    autonomousMode = 0;
    newState = 0;
    newActionAllowed = 1;

    robotPos = 1;
    armPos = 1;
  }
}

/**
 * Chooses an action based on the decision tree and the current mole states
 */
int selectAction(int moles[], GDecisionTree &dtree)
{
  // Copy mole states to array of doubles for passing to decision tree
  double states[9];
  int i;
  for(i=0; i<7; i++) {
    states[i] = (double) (moles[i]);
  } 
  // Add the robot and arm positions to the array
  states[7] = robotPos;
  states[8] = armPos;
  // Create a 1-long double array to hold chosen action
  double action[1];

  // Use the decision tree to select the action
  dtree.predict(states, action);

  // Extract value of action chosen
  ROS_INFO("Action: %f", action[0]);
  int chosenAction = (int) (action[0]);
  return chosenAction;
}

int main(int argc, char **argv)
{

  ros::init(argc, argv, "whackamole_learning_node");

  ros::NodeHandle n;
  
  ros::Publisher robotPosPub = n.advertise<std_msgs::Int16>("whackamole/cmd_robot_pos", 100);
  
  ros::Publisher armPosPub = n.advertise<std_msgs::Int16>("whackamole/cmd_arm_pos", 100);

  ros::Subscriber whackSub = n.subscribe("whackamole/whack_complete", 1000, whackCallback);
  
  ros::Subscriber robotArriveSub = n.subscribe("whackamole/robot_position_arrive", 1000, robotArriveCallback);

  ros::Subscriber stateSub = n.subscribe("whackamole/state_data", 1000, stateCallback);

  ros::Subscriber autoModeSub = n.subscribe("whackamole/autonomous_mode", 1000, autoModeCallback);

  ros::Subscriber gameStartedSub = n.subscribe("whackamole/game_started", 1000, gameStartedCallback);

  ros::Subscriber timeLeftSub = n.subscribe("whackamole/time_left", 1000, timeLeftCallback);
  
  // Set initial flag values
  gameStarted = 0;
  autonomousMode = 0;
  newState = 0;
  newActionAllowed = 1;
  treeInitialized = 0;

  robotPos = 1;
  armPos = 1;  

  // On startup, attempt to create the decision tree.
  GDecisionTree decisionTree;

  initializeTree(decisionTree);

  ros::Rate r(10);

  while(ros::ok()) {
 
    //ROS_INFO("%d %d %d %d %d", autonomousMode, newState, newActionAllowed, treeInitialized, gameStarted);
 
    // Only take actions if running in autonomous mode, the decision tree
    // has been initialized, a new mole state has been received since the 
    // last action, and a new action is allowed.
    if(autonomousMode && treeInitialized && newState && newActionAllowed && gameStarted) {
      // Selects an action based on the decision tree and the current mole    
      // states.
      int action = selectAction(moleStates, decisionTree);
      
      // Publishes the choice of action
      std_msgs::Int16 actionMessage;
      if(action >= 0 && action < 3) {
        // Arm position action
        actionMessage.data = action + 1; // Needs to publish as 1-3, not 0-2
        armPosPub.publish(actionMessage);
      } else if(action >= 3 && action < 6) {
        // Robot position action
        actionMessage.data = action - 2;
        robotPosPub.publish(actionMessage);
      }
      
      // Stops sending action commands until this action is complete and a new state is received.
      newActionAllowed = 0;
      newState = 0;
    }
 
    ros::spinOnce();
    r.sleep();
   }

  return 0;
}


