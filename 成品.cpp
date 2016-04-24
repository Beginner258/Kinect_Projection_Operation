#include <Windows.h>
#include <iostream>
#include <cstdio>
#include <Kinect.h>
#include <time.h>
#include <opencv2\core.hpp>
#include <opencv2\highgui.hpp>
#include <opencv2\imgproc.hpp>
#include <math.h>

using	namespace	std;
using	namespace	cv;

Vec3b	COLOR_TABLE[] = { Vec3b(255,0,0),Vec3b(0,255,0),Vec3b(0,0,255),Vec3b(255,255,255),Vec3b(0,0,0) ,Vec3b{ 255,0,255 } };
enum { BLUE, GREEN, RED, WHITE, BLACK, YELLOW };

const	int	MIN_DISTANCE = 25;				//���ͼ����Ļ��Ⱦ����С����,��λΪ����
const	int	MAX_DISTANCE = 250;				//���ͼ����Ļ��Ⱦ��������,��λΪ����
const	int	DEPTH_AVE_E = 10;				//����Ļƽ����Ⱥ��ô�ƽ�����ȥɸ�Ƿ������ֵ,��λΪ����
const	int	FINGER_DEPTH_E = 100;				//�ж���ָ�Ƿ�����Ļ��ֵ����λΪ����
const	int	FINGER_DISTANCE_E = 200;			//�ж���ָλ��ͻ����ֵ,��λΪ����
const	int	FINGER_MOVE_E = 2;				//�ж���ָ΢С������ֵ,��λΪ����
const	int	BGR_E = 45;					//��Ļʶ��ʱ�Ĳ�ɫ���,���255
const	int	AUTO_SEC = 5;					//�Զ�ѡȡʶ�����Ķ�����,���ó����·�VIS���������
int	COLORHEIGHT = 0, COLORWIDTH = 0;
int	DEPTHWIDTH = 0, DEPTHHEIGHT = 0;
bool	VIS[100] = { false };					//�������������

bool	find_edge(const Mat &, const Point &, int &, int &, int &, int &);
bool	check_depth_coordinate(int, int);
void	draw_screen(Mat &, int &, int &, int &, int &);
int	main(void)
{
	IKinectSensor	* mySensor = nullptr;
	GetDefaultKinectSensor(&mySensor);
	mySensor->Open();

	IFrameDescription	* myDescription = nullptr;

	IColorFrameSource	* myColorSource = nullptr;
	IColorFrameReader	* myColorReader = nullptr;
	IColorFrame		* myColorFrame = nullptr;
	mySensor->get_ColorFrameSource(&myColorSource);
	myColorSource->get_FrameDescription(&myDescription);
	myDescription->get_Height(&COLORHEIGHT);
	myDescription->get_Width(&COLORWIDTH);
	myColorSource->OpenReader(&myColorReader);			//����ΪColor֡��׼����ֱ�ӿ���Reader


	IDepthFrameSource	* myDepthSource = nullptr;
	IDepthFrameReader	* myDepthReader = nullptr;
	IDepthFrame		* myDepthFrame = nullptr;
	mySensor->get_DepthFrameSource(&myDepthSource);
	myDepthSource->get_FrameDescription(&myDescription);
	myDescription->get_Height(&DEPTHHEIGHT);
	myDescription->get_Width(&DEPTHWIDTH);
	myDepthSource->OpenReader(&myDepthReader);			//����ΪDepth֡��׼����ֱ�ӿ���Reader


	IBodyFrameSource	* myBodySource = nullptr;
	IBodyFrameReader	* myBodyReader = nullptr;
	IBodyFrame		* myBodyFrame = nullptr;
	mySensor->get_BodyFrameSource(&myBodySource);
	myBodySource->OpenReader(&myBodyReader);			//����ΪBody֡��׼����ֱ�ӿ���Reader


	ICoordinateMapper	* myMapper = nullptr;
	mySensor->get_CoordinateMapper(&myMapper);			//Maper��׼��



	int	colorLeft = 0, colorRight = 0, colorUp = 0, colorButtom = 0;
	int	depthLeft = 0, depthRight = 0, depthUp = 0, depthButtom = 0;
	DepthSpacePoint	depthLeftUp = { 0,0 }, depthRightButtom = { 0,0 };

	bool	gotColorCenter = false;
	bool	gotColorScreen = false;
	bool	gotDepthScreen = false;
	bool	gotFinger = false;
	bool	gotFrontFinger = false;
	int	basePoint = 5;
	Point	center = { COLORWIDTH / 2,COLORHEIGHT / 2 };

	Point	fingerPoint = { 0,0 };
	Point	frontFingerPoint = { -1,-1 };

	int	screenDepth = 0;
	bool	click = false;
	bool	failMessage = false;

	time_t	startTime = clock();
	time_t	nextUndoTime = 0;
	time_t	curTime = 0;
	bool	undoFlag = true;
	bool	work = false;
	bool	hint = true;
	bool	firstRun = true;
	while (1)
	{
		Mat	colorImg(COLORHEIGHT, COLORWIDTH, CV_8UC4);									//����ɫ����
		while (myColorReader->AcquireLatestFrame(&myColorFrame) != S_OK);
		myColorFrame->CopyConvertedFrameDataToArray(COLORHEIGHT * COLORWIDTH * 4, colorImg.data, ColorImageFormat_Bgra);


		Mat	depthImg(DEPTHHEIGHT, DEPTHWIDTH, CV_8UC3);									//���������
		UINT16	* depthData = new UINT16[DEPTHHEIGHT * DEPTHWIDTH];
		while (myDepthReader->AcquireLatestFrame(&myDepthFrame) != S_OK);
		myDepthFrame->CopyFrameDataToArray(DEPTHHEIGHT * DEPTHWIDTH, depthData);


		int	bodyCount;													//����������
		myBodySource->get_BodyCount(&bodyCount);
		IBody	** bodyArr = new IBody *[bodyCount];
		for (int i = 0; i < bodyCount; i++)
			bodyArr[i] = nullptr;
		while (myBodyReader->AcquireLatestFrame(&myBodyFrame) != S_OK);
		myBodyFrame->GetAndRefreshBodyData(bodyCount, bodyArr);

		//�����������л�ȡ����(ʵ����������)��״̬,�ж��Ƿ���
		bool	undo = false;
		double	spineHeight = 0, handHeight = 0;
		for (int i = 0; i < bodyCount; i++)
		{
			BOOLEAN		isTracked = false;
			if (bodyArr[i]->get_IsTracked(&isTracked) == S_OK && isTracked)
			{
				HandState	rightState;
				bodyArr[i]->get_HandRightState(&rightState);
				if (rightState == HandState_Closed)									//ȷ��Ҫ���ó���
					undo = true;

				Joint	* jointArr = new Joint[JointType_Count];
				bodyArr[i]->GetJoints(JointType_Count, jointArr);

				if (jointArr[JointType_SpineShoulder].TrackingState == TrackingState_Tracked)
					spineHeight = jointArr[JointType_SpineShoulder].Position.Y;
				if (jointArr[JointType_HandRight].TrackingState == TrackingState_Tracked)
					handHeight = jointArr[JointType_HandRight].Position.Y;

				delete[] jointArr;
			}
		}

		if (firstRun)
		{
			printf("�뽫Kinect�ڷ���ͶӰ�����ǰ��������ƽ����ͶӰ�棬Ȼ��ͶӰ����Ϊ��ɫ��ȫ����%d��󽫻��ڴ���ɫ����ͶӰ��ʶ��,����%d���鿴ʶ����\n", AUTO_SEC,AUTO_SEC);
			startTime = clock();
			firstRun = false;
			fill(VIS,VIS + 100,false);
		}

		//�Զ�����ѡȡ��Ļʶ���
		if (!gotColorCenter)
		{
			circle(colorImg, center, 15, COLOR_TABLE[RED], -1);					//������Ļ���ĵ�

			firstRun = false;
			curTime = clock();
			int	index = AUTO_SEC - (curTime - startTime) / CLOCKS_PER_SEC;
			if (index >= 0 && index <= AUTO_SEC)
			{
				if (!VIS[index])
				{
					printf("%d\n", index);
					VIS[index] = true;
				}
				if (!index)
				{
					gotColorCenter = true;
					puts("");
				}
			}
			goto	release;									//ҪReleaseһ�Σ���Ϊ��ʱ�Ѿ����㻭���˻����ϣ�ʶ��ᱻ���ϵĵ����
		}

		//��Ŀǰ��û��ʶ�����Ļ�������ѡ�е����ĵ�������Ļ
		if (!gotColorScreen && gotColorCenter)
			if (!find_edge(colorImg, center, colorLeft, colorRight, colorUp, colorButtom))
				goto	release;
			else
				gotColorScreen = true;


		if (gotColorScreen)
			draw_screen(colorImg, colorLeft, colorRight, colorUp, colorButtom);							//���˲�ɫ��Ļ�Ѿ��ҵ��ˣ�������Ļ�߿�

																		//�Ѳ�ɫ������ʶ�������Ļ������ת������ȿռ�,���������Ļ��ƽ������,ֻ��һ��											
		bool	mapSuccessed = true;
		if (!gotDepthScreen && !failMessage)
		{

			DepthSpacePoint		* colorMapArray = new DepthSpacePoint[COLORHEIGHT * COLORWIDTH];
			myMapper->MapColorFrameToDepthSpace(DEPTHHEIGHT * DEPTHWIDTH, depthData, COLORHEIGHT * COLORWIDTH, colorMapArray);

			//���ת����������Ƿ�Ϸ�
			if (check_depth_coordinate(colorMapArray[colorUp * COLORWIDTH + colorLeft].X, colorMapArray[colorUp * COLORWIDTH + colorLeft].Y))
				depthLeftUp = colorMapArray[colorUp * COLORWIDTH + colorLeft];
			else
				mapSuccessed = false;
			if (check_depth_coordinate(colorMapArray[colorButtom * COLORWIDTH + colorRight].X, colorMapArray[colorButtom * COLORWIDTH + colorRight].Y))
				depthRightButtom = colorMapArray[colorButtom * COLORWIDTH + colorRight];
			else
				mapSuccessed = false;
			depthLeft = (int)depthLeftUp.X, depthRight = (int)depthRightButtom.X, depthUp = (int)depthLeftUp.Y, depthButtom = (int)depthRightButtom.Y;


			if (!depthLeft || !depthRight || !depthUp || !depthButtom)							//���ת����������Ƿ�Ϊ0
				mapSuccessed = false;

			delete[] colorMapArray;
			//��������Ϸ�����ʼ�ж�����Ƿ�Ϸ�
			if (mapSuccessed)												//��һ��ɨ��������е�����ƽ��ֵ
			{
				int	sum = 0;
				int	count = 0;
				for (int i = depthUp + 10; i < depthButtom - 10; i++)
					for (int j = depthLeft + 10; j < depthRight - 10; j++)
					{
						int	index = i * DEPTHWIDTH + j;
						if (check_depth_coordinate(j, i))
						{
							sum += depthData[index];
							count++;
						}

					}
				if (!count)
					mapSuccessed = false;
				else
					screenDepth = sum / count;

				if (mapSuccessed)											//�ڶ���������������ƽ��ֵ��ɸȥ����Ƿ��ĵ�
				{
					sum = count = 0;
					for (int i = depthUp + 10; i < depthButtom - 10; i++)
						for (int j = depthLeft + 10; j < depthRight - 10; j++)
						{
							int	index = i * DEPTHWIDTH + j;
							if (check_depth_coordinate(j, i) && (depthData[index] - screenDepth) <= DEPTH_AVE_E)
							{
								sum += depthData[index];
								count++;
							}
						}
					if (!count)
						mapSuccessed = false;
					else
					{
						screenDepth = sum / count;								//��������Ļƽ������
						if (!screenDepth)
							mapSuccessed = false;
						else											//�������ꡢ��ȶ��Ϸ�
						{
							gotDepthScreen = true;
							printf("����ͷ��ͶӰ��ľ���=%.2lf��\n\n", (double)screenDepth / 1000);
						}
					}
				}
			}
		}

		//����Ļ�������ʧ�ܣ��������ʾ��Ϣ
		if (gotColorScreen && !gotDepthScreen && !failMessage)
		{
			printf("δ�ɹ��������ͶӰ��ľ��룬��ȷ��Kinectλ����ͶӰ����ǰ��2-3�״�����ͶӰ��ƽ�У�Ȼ��F5����ʶ��\n\n");
			failMessage = true;
		}

		//������ת���ɹ���������ͼ������Ⱦ,Ȼ�󻭳���Ļ,�����������ָ��
		gotFinger = false;
		fingerPoint = { 0,0 };
		if (mapSuccessed)
		{
			for (int i = depthUp; i < depthButtom; i++)									//��һ����Ⱦ
				for (int j = depthLeft; j < depthRight; j++)
				{
					int	index = i * DEPTHWIDTH + j;
					if (screenDepth - depthData[index] >= MIN_DISTANCE && screenDepth - depthData[index] <= MAX_DISTANCE)
						depthImg.at<Vec3b>(i, j) = COLOR_TABLE[GREEN];
					else
						depthImg.at<Vec3b>(i, j) = COLOR_TABLE[BLACK];
				}

			Mat	temp;													//�ڶ�����Ⱦ,ȥ����Ե
			depthImg.copyTo(temp);
			for (int i = depthUp; i < depthButtom; i++)
				for (int j = depthLeft; j < depthRight; j++)
				{
					bool	edge = false;
					for (int q = -1; q <= 1; q++)
						for (int e = -1; e <= 1; e++)
							if (check_depth_coordinate(i + q,j + 3) && depthImg.at<Vec3b>(i + q, j + e) == COLOR_TABLE[BLACK])
							{
								edge = true;
								temp.at<Vec3b>(i, j) = COLOR_TABLE[BLACK];
								goto	label;
							}
				label:
					if (!edge)
						temp.at<Vec3b>(i, j) = COLOR_TABLE[GREEN];
				}

			depthImg = temp;

			draw_screen(depthImg, depthLeft, depthRight, depthUp, depthButtom);						//��Ⱦ��󣬿�ʼ����Ļ��Ե


			for (int i = depthUp; i <= depthButtom; i++)									//ָ��ʶ��2��ȡ��ߵ�Ϊָ��
			{
				gotFinger = false;
				for (int j = depthLeft; j < depthRight; j++)
					if (depthImg.at<Vec3b>(i, j) == COLOR_TABLE[GREEN])
					{
						fingerPoint.x = j;
						fingerPoint.y = i;
						gotFinger = true;
						break;
					}
				if (gotFinger)
					break;
			}
			//�ж�ָ���λ���Ƿ�����Ļ����
			if (!(fingerPoint.x >= depthLeft && fingerPoint.y <= depthRight && fingerPoint.y >= depthUp && fingerPoint.y <= depthButtom))
				gotFinger = false;
		}

		//����ҵ���ָ�⣬���ָ����д���
		if (gotFinger)
		{
			if (frontFingerPoint.x == -1)
				frontFingerPoint = fingerPoint;
			//�ж��Ƿ��о���ͻ����
			if (sqrt(pow(fingerPoint.x - frontFingerPoint.x, 2) + pow(fingerPoint.y - frontFingerPoint.y, 2)) >= FINGER_DISTANCE_E
				&& depthImg.at<Vec3b>(frontFingerPoint.y, frontFingerPoint.x) == COLOR_TABLE[GREEN])
				fingerPoint = frontFingerPoint;
			//�ж��Ƿ��ƶ���΢С
			if ((abs(fingerPoint.x - frontFingerPoint.x) <= FINGER_MOVE_E && abs(fingerPoint.y - frontFingerPoint.y) <= FINGER_MOVE_E))
				fingerPoint = frontFingerPoint;
			else
				frontFingerPoint = fingerPoint;
		}


		//�������
		if (gotFinger && work)
		{
			fingerPoint.y += 1;
			int	fingerDepth = depthData[fingerPoint.y * DEPTHWIDTH + fingerPoint.x];
			fingerPoint.y -= 8;
			if (fingerPoint.y < 0)
				fingerPoint.y = 0;
			if (abs(fingerDepth - screenDepth) <= FINGER_DEPTH_E)
			{
				circle(depthImg, fingerPoint, 3, COLOR_TABLE[RED], -1);
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft),65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = true;
			}
			else
			{
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft), 65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = false;
			}
			gotFrontFinger = true;
		}
		//����궪ʧ����һ��
		else	if (gotFrontFinger && work)
		{
			fingerPoint = frontFingerPoint;
			int	fingerDepth = depthData[fingerPoint.y * DEPTHWIDTH + fingerPoint.x];
			if (click)
			{
				circle(depthImg, fingerPoint, 3, COLOR_TABLE[RED], -1);
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_MOVE | MOUSEEVENTF_LEFTDOWN, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft),65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = true;
			}
			else
			{
				mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 * (depthRight - fingerPoint.x) / (depthRight - depthLeft), 65535 * (fingerPoint.y - depthUp) / (depthButtom - depthUp), 0, 0);
				click = false;
			}
			gotFrontFinger = false;
		}
		if (!gotFinger)
			mouse_event(MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_LEFTUP, 65535 / 2, 65535 / 2, 0, 0);




		//�ж��Ƿ��˽�Flag����Ĵ���
		curTime = clock();
		if (curTime >= nextUndoTime && !undoFlag)
			undoFlag = true;
		//���ü��̳���
		if (undo && undoFlag && handHeight > spineHeight)
		{
			keybd_event(VK_CONTROL, 0, 0, 0);
			keybd_event(0x5A, 0, 0, 0);
			keybd_event(VK_CONTROL, 0, KEYEVENTF_KEYUP, 0);
			keybd_event(0x5A, 0, KEYEVENTF_KEYUP, 0);
			nextUndoTime = curTime + 500;											//��Flag��Ϊ�٣�������һ����Flag����Ĵ����趨��500ms��
			undoFlag = false;
		}


		for (int i = 0; i < DEPTHHEIGHT; i++)											//����������ת��
			for (int j = 0, k = DEPTHWIDTH - 1; j < DEPTHWIDTH / 2 + 1; j++, k--)
				swap(depthImg.at<Vec3b>(i, j), depthImg.at<Vec3b>(i, k));


		if (gotDepthScreen && !failMessage)
		{
			failMessage = true;
			printf("ʶ�����.��鿴�鿴COLOR���ڣ�ȷ��ͶӰ��ı�Ե�ͺ�ɫ������Ǻϣ�Ȼ��鿴DEPTH���ڣ�����Ƿ�����ɫ��ͼ�����.\n");
			printf("��ɫͼ��ĳ��ִ�����Kinect����ͷ����Ӧλ�þ�����Ļ̫����������ɫ������ڻ������·����ʹ�������ͷ��߱��ұ߸�������Ļ������������������Ļ��\n");
			printf("�����ͼ��΢��Kinect��λ�ã�ֱ����ɫ����ȫ��ʧ����ʱKinectӦ�û���ƽ����ͶӰ��.Ȼ��F5���½���.\n");
			printf("��ȷ������ɾ���ʶ������ȷ�ټ�����򣬼����Ὺ�������ĵ��ã���ɫ��Ĵ��ڻ�������λ��\n");
			printf("��������밴F1.\n\n");
		}
		if (GetKeyState(VK_F1) < 0)												//����Ч�������⣬���������һ��ѭ��������ʶ��
		{
			printf("���򼤻�!\n\n");
			work = true;
		}
		if (GetKeyState(VK_F5) < 0)												//����Ч�������⣬���������һ��ѭ��������ʶ��
		{
			gotColorCenter = gotColorScreen = gotDepthScreen = failMessage = work = false;
			firstRun = true;
		}

		release:														//��ʾͼ���ͷ�Frame

		imshow("COLOR", colorImg);
		imshow("DEPTH", depthImg);
		if (waitKey(30) == VK_ESCAPE)
			break;
		myColorFrame->Release();
		myDepthFrame->Release();
		myBodyFrame->Release();
		delete[] depthData;
		delete[] bodyArr;
	}



	myDepthReader->Release();		//�ͷ�Depth
	myDepthSource->Release();

	myColorReader->Release();		//�ͷ�Color
	myColorSource->Release();

	myBodyReader->Release();		//�ͷ�Body
	myBodySource->Release();

	myMapper->Release();			//�ͷŹ�����Դ
	myDescription->Release();

	mySensor->Close();			//�ͷ�Sensor
	mySensor->Release();


	return	0;
}


bool	find_edge(const Mat & img, const Point & center, int & left, int & right, int & up, int & buttom)
{
	Vec4b	screen = img.at<Vec4b>(center.y, center.x);

	int	front_i = -1, front_j = -1;
	for (int i = center.y; i < COLORHEIGHT; i++)
	{
		bool	flag = false;
		for (int j = center.x; j >= 0; j--)
			if (abs(img.at<Vec4b>(i, j)[0] - screen[0]) <= BGR_E && abs(img.at<Vec4b>(i, j)[1] - screen[1]) <= BGR_E && abs(img.at<Vec4b>(i, j)[2] - screen[2]) <= BGR_E && abs(img.at<Vec4b>(i, j)[3] - screen[3]) <= BGR_E)
			{
				flag = true;
				if (front_i == -1)
					front_i = i;
				if (front_j == -1)
					front_j = j;
				if (front_i < i && front_i != -1)
					front_i = i;
				if (front_j > j && front_j != -1)
					front_j = j;
			}
			else
				break;
		if (!flag)
			break;

	}
	buttom = front_i;
	left = front_j;

	front_i = -1;
	front_j = -1;
	for (int i = center.y; i >= 0; i--)
	{
		bool	flag = false;
		for (int j = center.x; j < COLORWIDTH; j++)
			if (abs(img.at<Vec4b>(i, j)[0] - screen[0]) <= BGR_E && abs(img.at<Vec4b>(i, j)[1] - screen[1]) <= BGR_E && abs(img.at<Vec4b>(i, j)[2] - screen[2]) <= BGR_E && abs(img.at<Vec4b>(i, j)[3] - screen[3]) <= BGR_E)
			{
				flag = true;
				if (front_i == -1)
					front_i = i;
				if (front_j == -1)
					front_j = j;
				if (front_i > i && front_i != -1)
					front_i = i;
				if (front_j < j && front_j != -1)
					front_j = j;
			}
			else
				break;
		if (!flag)
			break;
	}
	up = front_i;
	right = front_j;

	if (left == -1 || right == -1 || buttom == -1 || up == -1)
		return	false;
	return	true;
}

void	draw_screen(Mat & img, int & left, int & right, int & up, int & buttom)
{
	Point	p_1 = { left,up };
	Point	p_2 = { right,up };
	Point	p_3 = { right,buttom };
	Point	p_4 = { left,buttom };

	line(img, p_1, p_2, COLOR_TABLE[RED], 3);
	line(img, p_2, p_3, COLOR_TABLE[RED], 3);
	line(img, p_3, p_4, COLOR_TABLE[RED], 3);
	line(img, p_4, p_1, COLOR_TABLE[RED], 3);
}

bool	check_depth_coordinate(int x, int y)
{
	if (x >= 0 && x < DEPTHWIDTH && y >= 0 && y < DEPTHHEIGHT)
		return	true;
	return	false;
}
