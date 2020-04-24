
#include "inertial/PredictionModels.h"

// GT_PredictionModel

Matrix4d GT_PredictionModel::predict(Frame & curr_frame, 
                                     Frame & last_frame)
{
  Matrix4d new_pose = Matrix4d::Identity();

  // Attitude
  Quaterniond curr_R_gps(curr_frame.R_gps());
  Quaterniond last_R_gps(last_frame.R_gps());

  Quaterniond diff_R_gps = (curr_R_gps * last_R_gps.inverse()).normalized();
  Quaterniond new_rot = diff_R_gps * Quaterniond(last_frame.GetRotation()).normalized();
  new_pose.block<3,3>(0,0) = new_rot.normalized().toRotationMatrix();
  
  // Position
  Vector3d diff_t_gps = curr_frame.t_gps() - last_frame.t_gps();
  Vector3d translationSLAM = rotation_gps_to_slam * diff_t_gps;    
  Vector3d new_t = diff_R_gps * last_frame.GetPosition() + curr_R_gps.inverse() * translationSLAM;   

  new_pose.block<3,1>(0,3) = new_t;
  return new_pose;
}

void GT_PredictionModel::estimate_rotation_gps_to_slam(Frame & first_frame, 
                                                       Frame & curr_frame)
{
}


// --- IMU MODEL ---
IMU_model::IMU_model(const double & mad_gain):
  _pos_estimator(false),
  _att_estimator(mad_gain),
  _R_imu2w(Matrix3d::Identity()), 
  acc_lpf(LowPassFilter(3, 0.2)),
  ratio_lpf(LowPassFilter(1, 0.1)),
  it(0),
  _ratio(1)
{
  _pos_estimator.set_gravity(Vector3d(0.0, 0.0, 9.80655));

  // FOR TEST (use kitti matrix === Rot_nwu_to_cam)
  Matrix3d rotation_imu_to_velo;
  rotation_imu_to_velo << 9.999976e-01, 7.553071e-04, -2.035826e-03, 
                          -7.854027e-04, 9.998898e-01, -1.482298e-02,
                          2.024406e-03, 1.482454e-02,  9.998881e-01;
  Matrix3d rotation_velo_to_cam;
  rotation_velo_to_cam <<  7.967514e-03, -9.999679e-01, -8.462264e-04,
                          -2.771053e-03,  8.241710e-04, -9.999958e-01,
                            9.999644e-01,  7.969825e-03, -2.764397e-03;
  _R_imu2w = rotation_velo_to_cam * rotation_imu_to_velo;

  _last_vel.setZero();
  _linear_acc.setZero();
}

Matrix4d IMU_model::predict(IMU_Measurements & imu, double & dt, Frame & curr_frame, Frame & last_frame){
    // att
    _att_estimator.update(imu.acceleration(), imu.angular_velocity(), dt);
    Quaterniond att_cam = _att_estimator.get_local_orientation();  

    // pos
    Vector3d linear_acc;
    bool remove_grav = false;
    bool use_ratio = true;

    if(remove_grav){
      linear_acc = _pos_estimator.remove_gravity(imu.acceleration(), _att_estimator.get_orientation());
      linear_acc = (_R_imu2w * linear_acc); // acc on slam world
      linear_acc = acc_lpf.apply(linear_acc);
      _linear_acc = Vector3d(linear_acc);
      std::cout << "\tLINEAR ACCELERATION: " << linear_acc.transpose() << "- Norm:" << linear_acc.norm() << std::endl;
      //linear_acc /= 10;
    }
    else{
      linear_acc = imu.acceleration();
    }
    
    if (use_ratio && _ratio > 1e-10){
      //_ratio = 4.5; // 16.7
      linear_acc = linear_acc / (_ratio);
    }
    
    std::cout << "\tLINEAR ACCELERATION: " << linear_acc.transpose() << std::endl;
    Vector3d pos_world = _pos_estimator.update(linear_acc, dt); 
    Vector3d pos_cam = -(att_cam * pos_world);

    Matrix4d pose = Matrix4d::Identity();
    pose.block<3,3>(0,0) = att_cam.toRotationMatrix();
    pose.block<3,1>(0,3) = pos_cam;

    //test 
    position = pos_world;
    position_cam = pos_cam;
    attitude = _att_estimator.get_local_orientation();

    return pose;
  }

  void IMU_model::correct_pose(Frame & curr_frame, Frame & last_frame, double dt){
    int n = 1;
    if (it % n == 0){
      _att_estimator.set_orientation_from_frame(curr_frame.GetPose());
      Vector3d curr_pos = -curr_frame.GetRotation().transpose() * curr_frame.GetPosition(); 
      Vector3d last_pos = -last_frame.GetRotation().transpose() * last_frame.GetPosition(); 
      _pos_estimator.correct_pos_and_vel(curr_pos, last_pos, dt);
      
      // test
      Vector3d acc_corr =  (_pos_estimator.velocity() - _last_vel) / dt;
      std::cout << "\tACC correct: " << acc_corr.transpose() << "- Norm:" << acc_corr.norm() << std::endl;
      
      VectorXd ratio(1);
      ratio[0] = _linear_acc.norm() / acc_corr.norm();
      //_ratio = ratio_lpf.apply(ratio)[0];
      std::cout << "\tRatio: " << _ratio << std::endl;
      _last_vel = Vector3d(_pos_estimator.velocity());
    }
    it++;
  }
