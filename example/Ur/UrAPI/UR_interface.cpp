#include "UR_interface.h"

#include <cmath>
#include <iostream>
#include <fstream>
#include <iomanip>
#ifndef PI
#define PI 3.141592654
#endif
using namespace std;


UR_interface::UR_interface()
{
	connectedFlag = false;

}

UR_interface::~UR_interface(void)
{
	modbus_close(mb);
	modbus_free(mb);
	delete sender;
	//delete dashboard_sender;
}

bool UR_interface::isConnected()
{
	return connectedFlag;
}

//连接机器人
bool UR_interface::connect_robot(const std::string ip,int port)
{
	
	sender = new SocketClient(ip,port); //默认30003端口
	//dashboard_sender = new SocketClient(ip,29999);
        mb = modbus_new_tcp(ip.c_str(),MODBUS_TCP_DEFAULT_PORT);

	if(mb == NULL)
    {
        fprintf(stderr,"Unable to allocate libmodbus context\n");
        return false;
    }
		
	if( modbus_connect(mb) == -1)
	{
		fprintf(stderr,"Connection failed:%s\n",modbus_strerror(errno));
		modbus_free(mb);
        return false;
	}
	else
	{
		connectedFlag = true;
	}
    return true;
}



void UR_interface:: get_tcp_pos(double pos[6])
{
	uint16_t reg[6];
    
	modbus_read_registers(mb,400,6,reg); //查询地址400~405(10进制)6个寄存器中的数据

	for(int i = 0; i < 6; i++)
	{
		pos[i] = (double)reg[i];
		
		if( pos[i] > 32768)
            pos[i] = pos[i] - 65536;
        if (i < 3)
            pos[i] = pos[i] / 10;     //注意！单位是mm
        else
            pos[i] = pos[i] / 1000;
	}
}



void UR_interface::get_joint_angle(double joint_angle[6])
{
	uint16_t reg[6];

	modbus_read_registers(mb,270,6,reg); 

	for(int i = 0; i < 6; i++)
	{
		joint_angle[i] = (double)reg[i];

		if( joint_angle[i] > 32768)
			joint_angle[i] = joint_angle[i] - 65536;

		joint_angle[i] = joint_angle[i] / 1000;  //单位是弧度
	}
}


void UR_interface::axisangle_2_matrix(const double UR_AxisAngle[3], Matrix3d &m) //使用Eigen矩阵库
{
	double angle = 0;
	double c = 0;
	double s = 0;
	double t = 0;
	double x = 0;
	double y = 0;
	double z = 0;

	for(int i =0 ; i < 3; i++)  
	{
		angle += UR_AxisAngle[i] * UR_AxisAngle[i];
	}
	angle = sqrt(angle); //计算等效转角angle

	x = UR_AxisAngle[0] / angle;
	y = UR_AxisAngle[1] / angle;    // (x,y,z)为单位向量
	z = UR_AxisAngle[2] / angle;

	c = cos(angle);
	s = sin(angle);
	t = 1-c;

	m(0,0) = t*x*x + c;
	m(0,1) = t*x*y - z*s;
	m(0,2) = t*x*z + y*s;
	m(1,0) = t*x*y + z*s;
	m(1,1) = t*y*y + c;         //参考《机器人学导论》英文版第3版 P47 公式2.80
	m(1,2) = t*y*z - x*s;
	m(2,0) = t*x*z - y*s;
	m(2,1) = t*y*z + x*s;
	m(2,2) = t*z*z + c;
}

void UR_interface::UR6params_2_matrix(const double UR_6params[6],Matrix4d &m)
{
	double UR_AxisAngle[3] = {0,0,0};
	Matrix3d UR_RotationMatrix;

	for(int i = 0;i<3;i++)
		UR_AxisAngle[i] = UR_6params[3+i];   // UR_6params最后三个分量为axisangle

	axisangle_2_matrix(UR_AxisAngle,UR_RotationMatrix);

	for(int i = 0; i < 3; i++)
	{
		for(int j = 0; j < 3; j++)
		{	
			m(i,j) = UR_RotationMatrix(i,j);
		}
		m(3,i) = 0;
		m(i,3) = UR_6params[i];
	}

	m(3,3) = 1;

}


void UR_interface::matrix_2_axisangle(const Matrix3d &m, double UR_AxisAngle[3])
{
	double angle = 0;
	double temp1 = 0;
	double temp2 = 0;
	double temp3 = 0;
	double x =0;
	double y =0;
	double z =0;

	//need to be modified later
	double epsilon1 = 0.01;
	double epsilon2 = 0.1;
	//end 

	/*singularity found,first check for identity matrix which must have +1 for all terms
	  in leading diagonaland zero in other terms
	*/

	if ((abs(m(0,1)-m(1,0))< epsilon1)                           
		&& (abs(m(0,2)-m(2,0))< epsilon1)        //矩阵对称性检查（angle为0°或180°时矩阵为对称阵）
		&& (abs(m(1,2)-m(2,1))< epsilon1)) 
		/*singularity found,first check for identity matrix which must have +1 for all terms
	                 in leading diagonaland zero in other terms
	    */

	{
		if ((abs(m(0,1)+m(1,0)) < epsilon2)
			&& (abs(m(0,2)+m(2,0)) < epsilon2)
			&& (abs(m(1,2)+m(2,1)) < epsilon2)
			&& (abs(m(0,0)+m(1,1)+m(2,2)-3) < epsilon2))   //m近似为单位矩阵
	
		{
			for(int i = 0; i < 3; i++)
			{
				UR_AxisAngle[i] = 0;             //等效转角angle为0°
				
			}
			return;
		}
		
		
		// otherwise this singularity is angle = 180
		angle = PI;
		double xx = (m(0,0)+1)/2;
		double yy = (m(1,1)+1)/2;
		double zz = (m(2,2)+1)/2;
		double xy = (m(0,1)+m(1,0))/4;
		double xz = (m(0,2)+m(2,0))/4;
		double yz = (m(1,2)+m(2,1))/4;

		if ((xx > yy) && (xx > zz)) 
		{ // m(0,0) is the largest diagonal term
			if (xx< epsilon1) 
			{
				x = 0;
				y = 0.7071;
				z = 0.7071;
			} 
			else 
			{
				x = sqrt(xx);
				y = xy/x;
				z = xz/x;
			}
		} 
		else if (yy > zz) 
		{ 
			// m(1,1) is the largest diagonal term
			if (yy< epsilon1) 
			{
				x = 0.7071;
				y = 0;
				z = 0.7071;
			} 
			else 
			{
				y = sqrt(yy);
				x = xy/y;
				z = yz/y;
			}	
		} 
		else 
		{ // m(2,2) is the largest diagonal term so base result on this
			if (zz< epsilon1) 
			{
				x = 0.7071;
				y = 0.7071;
				z = 0;
			} 
			else 
			{
				z = sqrt(zz);
				x = xz/z;
				y = yz/z;
			}
		}
		// return 180 deg rotation
		UR_AxisAngle[0] = x*PI;
		UR_AxisAngle[1] = y*PI;
		UR_AxisAngle[2] = z*PI;
		return;
	}


	temp1 = pow((m(2,1)-m(1,2)),2) + pow((m(0,2)-m(2,0)),2) + pow((m(1,0)-m(0,1)),2);
	temp1 = sqrt(temp1);   // tmp1 = 2*sin(angle)    根据公式2.80中矩阵元素对称性可以计算出sin(angle)

	if (abs(temp1) < 0.001) 
		temp1 = 1; 
	// prevent divide by zero, should not happen if matrix is orthogonal and should be
	// caught by singularity test above, but I've left it in just in case
	angle = acos((m(0,0) + m(1,1) + m(2,2) - 1) / 2);

	x = (m(2,1) - m(1,2))/temp1;
	y = (m(0,2) - m(2,0))/temp1;
	z = (m(1,0) - m(0,1))/temp1;   //参考《机器人学导论》英文版第3版 P48 公式2.82
	UR_AxisAngle[0] = x*angle;
	UR_AxisAngle[1] = y*angle;
	UR_AxisAngle[2] = z*angle;
}



void UR_interface::matrix_2_UR6params(const Matrix4d &m,double UR_6params[6])
{
	Matrix3d m3x3;
	double AxisAngle[3];
	for(int i = 0;i<3;i++)
	{
		for(int j = 0;j<3;j++)
		{
			m3x3(i,j) = m(i,j);
		}
	}
	matrix_2_axisangle(m3x3,AxisAngle);

	for(int i = 0;i<3;i++)
	{
		UR_6params[i] = m(i,3);
		UR_6params[i+3] = AxisAngle[i];
	}
}


void UR_interface::print_matrix(const MatrixXd &m,std::string filename)//打印矩阵元素
{
	std::ofstream outfile(filename,std::ios::out);
	for(int i = 0; i < m.rows(); ++i)
	{
		for(int j = 0; j < m.cols(); ++j)
		{
			//if( abs(m(i,j)) < 1e-10 )//矩阵元素若小于1e-10则将其置为0
			//	m(i,j) = 0;
			outfile<<std::setprecision(9)<<std::setiosflags(std::ios::left|std::ios::scientific)
				<<std::setw(24)<<m(i,j);//域宽24,采用科学计数法，左对齐，9位精度
		}
	outfile<<std::endl;
	}
}



void UR_interface::movej_joint(double joint_angle[6],float a, float v, float t, float r) //注意：带默认参数的函数,声明和定义两者默认值只能在一个地方出现，不能同时出现。
{
	stringstream temp;   //创建一个流
	string cmd;
	temp<<"movej(["<<joint_angle[0]<<","<<joint_angle[1]<<","<<joint_angle[2]<<","<<joint_angle[3]<<","<<joint_angle[4]<<","<<joint_angle[5]<<"],"
		<<a<<","<<v<<","<<t<<","<<r<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::movej_pose(double tcp_pose[6],float a, float v, float t, float r) 
{
	stringstream temp;   
	string cmd;
	temp<<"movej(p["<<tcp_pose[0]<<","<<tcp_pose[1]<<","<<tcp_pose[2]<<","<<tcp_pose[3]<<","<<tcp_pose[4]<<","<<tcp_pose[5]<<"],"
		<<a<<","<<v<<","<<t<<","<<r<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::movel_pose(double tcp_pose[6],float a, float v, float t, float r) 
{
	stringstream temp;   
	string cmd;
	temp<<"movej(p["<<tcp_pose[0]<<","<<tcp_pose[1]<<","<<tcp_pose[2]<<","<<tcp_pose[3]<<","<<tcp_pose[4]<<","<<tcp_pose[5]<<"],"
		<<a<<","<<v<<","<<t<<","<<r<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::movej_tool(double relative_pose[6],float a, float v,float t,float r) 
{
	stringstream temp;   
	string cmd;
	temp<<"def f():\n"
		<<"wp1=get_forward_kin()\n"
		<<"movej(pose_trans(wp1,p["<<relative_pose[0]<<","<<relative_pose[1]<<","<<relative_pose[2]<<","
		<<relative_pose[3]<<","<<relative_pose[4]<<","<<relative_pose[5]<<"]),"<<a<<","<<v<<","<<t<<","<<r<<")\n"
		<<"end\n";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::movel_tool(double relative_pose[6],float a, float v,float t,float r) 
{
	stringstream temp;   
	string cmd;
	temp<<"def f():\n"
		<<"wp1=get_forward_kin()\n"
		<<"movel(pose_trans(wp1,p["<<relative_pose[0]<<","<<relative_pose[1]<<","<<relative_pose[2]<<","
		<<relative_pose[3]<<","<<relative_pose[4]<<","<<relative_pose[5]<<"]),"<<a<<","<<v<<","<<t<<","<<r<<")\n"
		<<"end\n";
	cmd = temp.str();
    sender->SendLine(cmd);
}

void UR_interface::yb_movep_tool(double relative_pose[], float a, float v, float t, float r)
{
    stringstream temp;
    string cmd;
    temp<<"def f():\n"
        <<"wp1=get_forward_kin()\n"
        <<"movej(pose_trans(wp1,p["<<relative_pose[0]<<","<<relative_pose[1]<<","<<relative_pose[2]<<","
        <<relative_pose[3]<<","<<relative_pose[4]<<","<<relative_pose[5]<<"]),"<<a<<","<<v<<","<<t<<","<<r<<")\n"
        <<"end\n";
    cmd = temp.str();
    sender->SendLine(cmd);
}

void UR_interface::yb_movep_TCP(double relative_pose[], float a, float v, float t, float r)
{
    stringstream temp;

    string cmd;
    temp<<"def f():\n"
        <<"wp1=get_actual_tcp_pose()\n"
        <<"movej(pose_trans(wp1,p["<<relative_pose[0]<<","<<relative_pose[1]<<","<<relative_pose[2]<<","
        <<relative_pose[3]<<","<<relative_pose[4]<<","<<relative_pose[5]<<"]),"<<a<<","<<v<<","<<t<<","<<r<<")\n"
        <<"end\n";
    cmd = temp.str();
    sender->SendLine(cmd);
}


void UR_interface::yb_movej(double joint_angle[], float a, float v, float t, float r)
{

    stringstream temp;   //创建一个流
    string cmd;
    temp<<"movej(["<<joint_angle[0]<<","<<joint_angle[1]<<","<<joint_angle[2]<<","<<joint_angle[3]<<","<<joint_angle[4]<<","<<joint_angle[5]<<"],"
        <<a<<","<<v<<","<<t<<","<<r<<")";
    cmd = temp.str();
    sender->SendLine(cmd);
}

void UR_interface::yb_movej_relative(double joint_angle[], float a, float v, float t, float r)
{
    stringstream temp;   //创建一个流
    string cmd;
    temp<<"def f():\n"
       <<"b1=get_actual_joint_positions()\n"
       <<"b1[0] = b1[0] +" <<joint_angle[0]<<"\n"
       <<"b1[1] = b1[1] +" <<joint_angle[1]<<"\n"
       <<"b1[2] = b1[2] +" <<joint_angle[2]<<"\n"
       <<"b1[3] = b1[3] +" <<joint_angle[3]<<"\n"
       <<"b1[4] = b1[4] +" <<joint_angle[4]<<"\n"
       <<"b1[5] = b1[5] +" <<joint_angle[5]<<"\n"
       <<"movej(b1,"<<a <<"," << v<<","<<t<<","<<r<<")\n"
       <<"end\n";

    cmd = temp.str();
    sender->SendLine(cmd);


}

void UR_interface::yb_get_joint_angle(double joint_angle[])
{
    uint16_t reg[6];

    modbus_read_registers(mb,270,6,reg);

    for(int i = 0; i < 6; i++)
    {
        joint_angle[i] = (double)reg[i];

        if( joint_angle[i] > 32768)
            joint_angle[i] = joint_angle[i] - 65536;

        joint_angle[i] = joint_angle[i] / 1000;  //单位是弧度
    }

    for(int i = 0 ;i < 6 ;i++)
    {
        if(joint_angle[i] > PI )
            joint_angle[i] -= 2*PI;
        if(joint_angle[i] < -PI )
            joint_angle[i] += 2*PI;
    }
}


//回零
void UR_interface::go_home(float a, float v)//定义中不再给出默认值
{
	stringstream temp;   
	string cmd;
	temp<<"movej([0.00,-1.5708,0.00,-1.5708,0.00,0.00]," <<a<<","<<v<<")";  //UR5零位时的joint_angle值
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::move_circle(double diameter,int count,float a, float v) //直径单位是m
{
	if (diameter == 0)	return;
	stringstream temp;   
	string cmd;
	temp<<"def f():\n"
		<<"i=0\n"
		<<	" while i<"<<count<<":\n"
		<<	" wp1=get_forward_kin()\n"
		<<	" wp2 = pose_trans(wp1,p["<<diameter/2<<","<<diameter/2<<",0,0,0,0])\n"
		<<	" wp3 = pose_trans(wp1,p[0,"<<diameter<<",0,0,0,0])\n"
		<<	" wp4 = pose_trans(wp1,p["<<-diameter/2<<","<<diameter/2<<",0,0,0,0])\n"
		<<	" movep(wp1,"<<a<<","<<v<<",r=0.001)\n"
		<<	" movec(wp2,wp3,"<<a<<","<<v<<",r=0.001)\n"
		<<	" movec(wp4,wp1,"<<a<<","<<v<<",r=0.001)\n"
		<<	" i=i+1\n"
		<<	" end\n"
		<<"end\n"
		<<"f()";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::move_spiral(double diameter,double pitch,double depth,float a, float v) //注意！单位是m
{
	if( diameter == 0 ||pitch ==0 || depth == 0)
		return;
	int count = depth / pitch;  //螺旋钻孔圈数
	stringstream temp;   
	string cmd;
	temp<<"def drill():\n"      //钻孔深度方向为机器人工具坐标系Z轴方向,如需更换为Y向或X向,请修改下列指令语句
		<<"i=0\n"
		<<	" while i<"<<count<<":\n"
		<<	" wp1=get_forward_kin()\n"
		<<	" wp2=pose_trans(wp1,p["<<diameter/2<<","<<diameter/2<<","<<pitch/4<<",0,0,0])\n"
		<<	" wp3=pose_trans(wp1,p[0,"<<diameter<<","<<pitch/2<<",0,0,0])\n"
		<<	" wp4=pose_trans(wp1,p["<<-diameter/2<<","<<diameter/2<<","<<3*pitch/4<<",0,0,0])\n" 
		<<	" wp5=pose_trans(wp1,p[0,0,"<<pitch<<",0,0,0])\n"   
		<<	" movep(wp1,a=0.8,v=0.04,r=0.001)\n"
		<<	" movec(wp2,wp3,"<<a<<","<<v<<",r=0.001)\n"
		<<	" movec(wp4,wp5,"<<a<<","<<v<<",r=0.001)\n"
		<<	" i=i+1\n"
		<<	" end\n"
		<<"end\n"
		<<"drill()";
	cmd = temp.str();
	sender->SendLine(cmd);
}

//运动 Base Pose 中的 X+
void UR_interface::speedl(double tcp_speed[6],float a, float t)
{
	stringstream temp;   
	string cmd;
	temp<<"speedl(["<<tcp_speed[0]<<","<<tcp_speed[1]<<","<<tcp_speed[2]<<","<<tcp_speed[3]<<","<<tcp_speed[4]<<","<<tcp_speed[5]<<"],"
		<<a<<","<<t<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::speedj(double tcp_speed[6], float a, float t)
{
	stringstream temp;   
	string cmd;
	temp<<"speedj(["<<tcp_speed[0]<<","<<tcp_speed[1]<<","<<tcp_speed[2]<<","<<tcp_speed[3]<<","<<tcp_speed[4]<<","<<tcp_speed[5]<<"],"
		<<a<<","<<t<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}



void UR_interface::stopl(float a) 
{
	stringstream temp;   
	string cmd;
	temp<<"stopl("<<a<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::stopj(float a) 
{
	stringstream temp;   
	string cmd;
	temp<<"stopj("<<a<<")";
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::power_off_robot()
{
	//dashboard_sender->SendLine("shutdown"); //两种方式关机
	sender->SendLine("powerdown()");   //Shutdown the robot, and power off the robot and controller.
}


void UR_interface::set_real()
{
	sender->SendLine("set real");
}


void UR_interface::set_sim()
{
	sender->SendLine("set sim");
}


void UR_interface::set_speed(double ratio)
{
	stringstream temp;   
	string cmd;
	temp<<"set speed"<<" "<<ratio;  //ratio为0~1之间的一个小数
	cmd = temp.str();
	sender->SendLine(cmd);
}


void UR_interface::set_robot_mode(int mode)
{
//    switch(mode)
//    {
//        case ROBOT_RUNNING_MODE:
//            sender->SendLine("set robotmode run");
//            break;
//        case ROBOT_FREEDRIVE_MODE:
//            sender->SendLine("set robotmode freedrive"); //设置为示教模式
//            break;
//        default:break;
//    }
    switch(mode)
        {
            case ROBOT_RUNNING_MODE:
    //            sender->SendLine("set robotmode run");
            sender->SendLine("end_teach_mode()");//UR10
                break;
            case ROBOT_FREEDRIVE_MODE:
    //            sender->SendLine("set robotmode freedrive"); //设置为示教模式
            this->teachmode();//UR10
            break;
            default:break;
        }
}

void UR_interface::teachmode()
{
    stringstream temp;
    string cmd;
    temp<<"def teach():\n"
        <<"	while(True):\n"
        <<"		teach_mode()\n"
        <<"		sleep(0.01)\n"
        <<"	end\n"
        <<"end\n"
       <<"teach()\n";
    cmd = temp.str();
    sender->SendLine(cmd);
}
int UR_interface::get_robot_mode()
{
	uint16_t reg[1];
	modbus_read_registers(mb,258,1,reg); 
	
	return reg[0];  //返回值0-9代表不同机器人模式
}


int UR_interface::is_security_stopped()
{
	uint8_t state[1];
	modbus_read_bits(mb,261,1,state); //The function uses the Modbus function code 0x01 (read coil status)
	
	return state[0]; 
}


int UR_interface::is_emergency_stopped()
{
	uint8_t state[1];
	modbus_read_bits(mb,262,1,state); 
	
	return state[0]; 
}

void UR_interface::set_tcp_pos(double pos[6])
{
    stringstream temp;   //创建一个流
    string cmd;
    temp<<"set_tcp(p["<<pos[0]<<","<<pos[1]<<","<<pos[2]<<","<<pos[3]<<","<<pos[4]<<","<<pos[5]<<"])";
    cmd = temp.str();
    sender->SendLine(cmd);
}
