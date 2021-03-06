#include <exception>
#include "ofxRSSDKv2.h"
#include "ofMain.h"

namespace ofxRSSDK
{
	RSDevice::~RSDevice() {}
	RSDevice::RSDevice()
	{
		mIsInit = false;
		mIsRunning = false;
		mIsMirrored = false;
		mHasRgb = false;
		mHasDepth = false;
		mShouldAlign = false;
		mShouldGetDepthAsColor = false;
		mShouldGetPointCloud = false;
		mShouldGetFaces = false;
		mShouldGetBlobs = false;
		mPointCloudRange = ofVec2f(0, 3000);
		mCloudRes = CloudRes::FULL_RES;
	}

#pragma region Init
	bool RSDevice::init()
	{
		mSenseMgr = PXCSenseManager::CreateInstance();
		if (mSenseMgr)
			mIsInit = true;

		return mIsInit;
	}

	bool RSDevice::initRgb(const RGBRes& pSize, const float& pFPS)
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			switch (pSize)
			{
			case RGBRes::SM:
				mRgbSize = ofVec2f(320, 240);
				break;
			case RGBRes::VGA:
				mRgbSize = ofVec2f(640, 480);
				break;
			case RGBRes::HD720:
				mRgbSize = ofVec2f(1280, 720);
				break;
			case RGBRes::HD1080:
				mRgbSize = ofVec2f(1920, 1080);
				break;
			}
			cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_COLOR, mRgbSize.x, mRgbSize.y, pFPS);
			if (cStatus >= PXC_STATUS_NO_ERROR)
			{
				mHasRgb = true;
				mRgbFrame.allocate(mRgbSize.x, mRgbSize.y, ofPixelFormat::OF_PIXELS_BGRA);
			}
		}

		return mHasRgb;
	}

	bool RSDevice::initDepth(const DepthRes& pSize, const float& pFPS, bool pAsColor)
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			switch (pSize)
			{
			case DepthRes::R200_SD:
				mDepthSize = ofVec2f(480, 360);
				break;
			case DepthRes::R200_VGA:
				mDepthSize = ofVec2f(628, 468);
				break;
			case DepthRes::F200_VGA:
				mDepthSize = ofVec2f(640, 480);
				break;
			case DepthRes::QVGA:
				mDepthSize = ofVec2f(320, 240);
				break;
			}
			cStatus = mSenseMgr->EnableStream(PXCCapture::STREAM_TYPE_DEPTH, mDepthSize.x, mDepthSize.y, pFPS);
			if (cStatus >= PXC_STATUS_NO_ERROR)
			{
				mHasDepth = true;
				mShouldGetDepthAsColor = pAsColor;
				mDepthFrame.allocate(mDepthSize.x, mDepthSize.y, 1);
				mDepth8uFrame.allocate(mDepthSize.x, mDepthSize.y, ofPixelFormat::OF_PIXELS_RGBA);
				mRawDepth = new uint16_t[(int)mDepthSize.x*(int)mDepthSize.y];
			}
		}
		return mHasDepth;
	}
#pragma endregion

	void RSDevice::setPointCloudRange(float pMin = 100.0f, float pMax = 1500.0f)
	{
		mPointCloudRange = ofVec2f(pMin, pMax);
	}

	// can set this before or after start - but SDK only allows after init, so if not running, defer
	void RSDevice::setMirrored(bool isMirrored) {
		mIsMirrored = isMirrored;
		if (!mIsRunning) return;
		if (mIsMirrored)
			mSenseMgr->QueryCaptureManager()->QueryDevice()->SetMirrorMode(PXCCapture::Device::MirrorMode::MIRROR_MODE_HORIZONTAL);
		else 
			mSenseMgr->QueryCaptureManager()->QueryDevice()->SetMirrorMode(PXCCapture::Device::MirrorMode::MIRROR_MODE_DISABLED);
	}

	bool RSDevice::start()
	{
		pxcStatus cStatus = mSenseMgr->Init();
		if (cStatus >= PXC_STATUS_NO_ERROR)
		{
			mCoordinateMapper = mSenseMgr->QueryCaptureManager()->QueryDevice()->CreateProjection();
			if (mShouldAlign)
			{
				mColorToDepthFrame.allocate(mRgbSize.x, mRgbSize.y, ofPixelFormat::OF_PIXELS_RGBA);
				mDepthToColorFrame.allocate(mRgbSize.x, mRgbSize.y, ofPixelFormat::OF_PIXELS_RGBA);
			}
			mIsRunning = true;
			setMirrored(mIsMirrored);
			return true;
		}
		return false;
	}

	bool RSDevice::update()
	{
		pxcStatus cStatus;
		if (mSenseMgr)
		{
			cStatus = mSenseMgr->AcquireFrame(false, 0);
			if (cStatus < PXC_STATUS_NO_ERROR)
				return false;
			PXCCapture::Sample *mCurrentSample = mSenseMgr->QuerySample();
			if (!mCurrentSample)
				return false;
			if (mHasRgb)
			{
				if (!mCurrentSample->color)
					return false;
				PXCImage *cColorImage = mCurrentSample->color;
				PXCImage::ImageData cColorData;
				cStatus = cColorImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cColorData);
				if (cStatus < PXC_STATUS_NO_ERROR)
				{
					cColorImage->ReleaseAccess(&cColorData);
					return false;
				}
				mRgbFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cColorData.planes[0]), mRgbSize.x, mRgbSize.y, 4);

				cColorImage->ReleaseAccess(&cColorData);
				if (!mHasDepth)
				{
					mSenseMgr->ReleaseFrame();
					return true;
				}
			}
			if (mHasDepth)
			{
				if (!mCurrentSample->depth)
					return false;
				PXCImage *cDepthImage = mCurrentSample->depth;
				PXCImage::ImageData cDepthData;
				cStatus = cDepthImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_DEPTH, &cDepthData);

				if (cStatus < PXC_STATUS_NO_ERROR)
				{
					cDepthImage->ReleaseAccess(&cDepthData);
					return false;
				}
				mDepthFrame.setFromExternalPixels(reinterpret_cast<uint16_t *>(cDepthData.planes[0]), mDepthSize.x, mDepthSize.y, 1);
				memcpy(mRawDepth, reinterpret_cast<uint16_t *>(cDepthData.planes[0]), (size_t)((int)mDepthSize.x*(int)mDepthSize.y * sizeof(uint16_t)));
				cDepthImage->ReleaseAccess(&cDepthData);

				if (mShouldGetDepthAsColor)
				{
					PXCImage::ImageData cDepth8uData;
					cStatus = cDepthImage->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cDepth8uData);
					if (cStatus < PXC_STATUS_NO_ERROR)
					{
						cDepthImage->ReleaseAccess(&cDepth8uData);
						return false;
					}
					mDepth8uFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cDepth8uData.planes[0]), mDepthSize.x, mDepthSize.y, 4);
					cDepthImage->ReleaseAccess(&cDepth8uData);
				}

				if (mShouldGetPointCloud)
				{
					updatePointCloud();
				}

				if (!mHasRgb)
				{
					mSenseMgr->ReleaseFrame();
					return true;
				}
			}

			if (mHasDepth && mHasRgb && mShouldAlign && mAlignMode == AlignMode::ALIGN_FRAME)
			{
				PXCImage *cMappedColor = mCoordinateMapper->CreateColorImageMappedToDepth(mCurrentSample->depth, mCurrentSample->color);
				PXCImage *cMappedDepth = mCoordinateMapper->CreateDepthImageMappedToColor(mCurrentSample->color, mCurrentSample->depth);

				if (!cMappedColor || !cMappedDepth)
					return false;

				PXCImage::ImageData cMappedColorData;
				if (cMappedColor->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cMappedColorData) >= PXC_STATUS_NO_ERROR)
				{
					mColorToDepthFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cMappedColorData.planes[0]), mRgbSize.x, mRgbSize.y, 4);
					cMappedColor->ReleaseAccess(&cMappedColorData);
				}

				PXCImage::ImageData cMappedDepthData;
				if (cMappedDepth->AcquireAccess(PXCImage::ACCESS_READ, PXCImage::PIXEL_FORMAT_RGB32, &cMappedDepthData) >= PXC_STATUS_NO_ERROR)
				{
					mDepthToColorFrame.setFromExternalPixels(reinterpret_cast<uint8_t *>(cMappedDepthData.planes[0]), mRgbSize.x, mRgbSize.y, 4);
					cMappedDepth->ReleaseAccess(&cMappedDepthData);
				}

				try
				{
					cMappedColor->Release();
					cMappedDepth->Release();
				}
				catch (const exception &e)
				{
					ofLog(ofLogLevel::OF_LOG_WARNING, "Release check error: ");
					ofLog(ofLogLevel::OF_LOG_WARNING, e.what());
				}
			}
			if (mShouldGetBlobs)
			{
				clearBlobs();
				updateBlobs();
			}
			mSenseMgr->ReleaseFrame();
			return true;
		}
		return false;
	}

	bool RSDevice::stop()
	{
		if (mSenseMgr)
		{
			mCoordinateMapper->Release();
			mSenseMgr->Close();
			if (mShouldGetBlobs)
			{
				if (mBlobTracker) {
					clearBlobs();
					//mBlobTracker->Release(); SDK says don't release this
				}
			}
			if (mShouldGetFaces)
			{
				if (mFaceTracker)
					mFaceTracker->Release();
			}
			return true;
		}
		delete[] mRawDepth;
		return false;
	}

#pragma region Enable
	bool RSDevice::enableFaceTracking(bool pUseDepth)
	{
		if (mSenseMgr)
		{
			if (mSenseMgr->EnableFace() >= PXC_STATUS_NO_ERROR)
			{
				mFaceTracker = mSenseMgr->QueryFace();
				if (mFaceTracker)
				{
					PXCFaceConfiguration *config = mFaceTracker->CreateActiveConfiguration();
					switch (pUseDepth)
					{
					case true:
						config->SetTrackingMode(PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR_PLUS_DEPTH);
						break;
					case false:
						config->SetTrackingMode(PXCFaceConfiguration::TrackingModeType::FACE_MODE_COLOR);
						break;
					}
					config->ApplyChanges();
					config->Release();
					mShouldGetFaces = true;
				}
				else
					mShouldGetFaces = false;
				return mShouldGetFaces;
			}
			return false;
		}
		return false;
	}

	bool RSDevice::enableBlobTracking()
	{
		if (mSenseMgr)
		{
			if (mSenseMgr->EnableBlob() >= PXC_STATUS_NO_ERROR)
			{
				mBlobTracker = mSenseMgr->QueryBlob();
				if (mBlobTracker) {
					mBlobData = mBlobTracker->CreateOutput();
					PXCBlobConfiguration* config = mBlobTracker->CreateActiveConfiguration();
					config->SetMaxBlobs(4);
					config->ApplyChanges();
					config->Release();
					mShouldGetBlobs = true;
				} else {
					mShouldGetBlobs = false;
				}

				return mShouldGetBlobs;
			}
			return false;
		}
		return false;

	}
#pragma endregion

#pragma region Update
	void RSDevice::updatePointCloud()
	{
		int width = (int)mDepthSize.x;
		int height = (int)mDepthSize.y;
		int step = (int)mCloudRes;
		mPointCloud.clear();
		vector<PXCPoint3DF32> depthPoints, worldPoints;
		for (int dy = 0; dy < height; dy += step)
		{
			for (int dx = 0; dx < width; dx += step)
			{
				PXCPoint3DF32 cPoint;
				cPoint.x = dx; cPoint.y = dy; cPoint.z = (float)mRawDepth[dy*width + dx];
				if (cPoint.z>mPointCloudRange.x&&cPoint.z<mPointCloudRange.y)
					depthPoints.push_back(cPoint);
			}
		}

		worldPoints.resize(depthPoints.size());
		mCoordinateMapper->ProjectDepthToCamera(depthPoints.size(), &depthPoints[0], &worldPoints[0]);

		for (int i = 0; i < depthPoints.size();++i)
		{
			PXCPoint3DF32 p = worldPoints[i];
			mPointCloud.push_back(ofVec3f(p.x, p.y, p.z));
		}
	}

	void RSDevice::updateBlobs()
	{
		pxcStatus results = PXC_STATUS_NO_ERROR;

		// Get extracted blobs
		mBlobData->Update();

		int numBlobs = mBlobData->QueryNumberOfBlobs();
		mBlobs.resize(numBlobs); // instead use mBlobs.reserve(4) in setup since never more than 4 blobs
		mBlobContourSizes.resize(numBlobs);
		mBlobImages.resize(numBlobs);

		for (int i = 0; i < numBlobs; i++)
		{
			PXCBlobData::IBlob * blob = NULL;
			mBlobData->QueryBlob(i, PXCBlobData::SEGMENTATION_IMAGE_DEPTH, PXCBlobData::ACCESS_ORDER_RIGHT_TO_LEFT, blob);

			// not sure if this works but could come in handy
			blob->QuerySegmentationImage(mBlobImages[i]);

			int numContours = blob->QueryNumberOfContours();
			mBlobs[i].resize(numContours);
			mBlobContourSizes[i].resize(numContours);

			if (numContours > 0)
			{
				//for (int j = 0; j < numContours; ++j)
				for (int j = 0; j < 1; j++) // temporary, only first contour
				{
					PXCBlobData::IContour * contour;
					if (blob->QueryContour(j, contour) == pxcStatus::PXC_STATUS_NO_ERROR)
					{
						mBlobContourSizes[i].at(j) = 0;
						int numPoints = contour->QuerySize();
						mBlobs[i].at(j) = 0;
						if (numPoints > 0)
						{
							// this memory is freed in clearBlobs()
							mBlobs[i].at(j) = new PXCPointI32[numPoints];
							results = contour->QueryPoints(numPoints, mBlobs[i].at(j));
							if (results != PXC_STATUS_NO_ERROR) continue;
							mBlobContourSizes[i].at(j) = numPoints;
						}
					}
				}
			}
		}
	}

	void RSDevice::clearBlobs()
	{
		for (int i = 0; i < mBlobs.size(); i++)
		{
			for (int j = 0; j < mBlobs[i].size(); j++)
			{
				if (mBlobs[i].at(j) != NULL)
				{
					delete[] mBlobs[i].at(j);
					mBlobs[i].at(j) = NULL;
				}

			}
			mBlobs[i].clear();
		}
	}
#pragma endregion

#pragma region Getters
	const ofPixels& RSDevice::getRgbFrame()
	{
		return mRgbFrame;
	}

	const ofShortPixels& RSDevice::getDepthFrame()
	{
		return mDepthFrame;
	}

	const ofPixels& RSDevice::getDepth8uFrame()
	{
		return mDepth8uFrame;
	}

	const ofPixels& RSDevice::getColorMappedToDepthFrame()
	{
		return mColorToDepthFrame;
	}

	const ofPixels& RSDevice::getDepthMappedToColorFrame()
	{
		return mDepthToColorFrame;
	}

	vector<ofVec3f> RSDevice::getPointCloud()
	{
		return mPointCloud;
	}

	vector<vector<PXCPointI32*>> RSDevice::getBlobs()
	{
		return mBlobs;
	}

	vector<vector<int>> RSDevice::getBlobContourSizes()
	{
		return mBlobContourSizes;
	}

	vector<PXCImage*> RSDevice::getBlobImages()
	{
		return mBlobImages;
	}

	//Nomenclature Notes:
	//	"Space" denotes a 3d coordinate
	//	"Image" denotes an image space point ((0, width), (0,height), (image depth))
	//	"Coords" denotes texture space (U,V) coordinates
	//  "Frame" denotes a full Surface

	//get a camera space point from a depth image point
	const ofPoint RSDevice::getDepthSpacePoint(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			mInPoints3D.clear();
			mInPoints3D.push_back(cPoint);
			mOutPoints3D.clear();
			mOutPoints3D.resize(2);
			mCoordinateMapper->ProjectDepthToCamera(1, &mInPoints3D[0], &mOutPoints3D[0]);
			return ofPoint(mOutPoints3D[0].x, mOutPoints3D[0].y, mOutPoints3D[0].z);
		}
		return ofPoint(0);
	}

	const ofPoint RSDevice::getDepthSpacePoint(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getDepthSpacePoint(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const ofPoint RSDevice::getDepthSpacePoint(ofPoint pImageCoords)
	{
		return getDepthSpacePoint(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

	//get a Color object from a depth image point
	const ofColor RSDevice::getColorFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;
			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			if (cColorX >= 0 && cColorX < mRgbSize.x&&cColorY >= 0 && cColorY < mRgbSize.y)
			{
				return mRgbFrame.getColor(cColorX, cColorY);
			}
		}
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		if (mCoordinateMapper)
			return getColorFromDepthImage(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthImage(ofPoint pImageCoords)
	{
		if (mCoordinateMapper)
			return getColorFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
		return ofColor::black;
	}


	//get a ofColor object from a depth camera space point
	const ofColor RSDevice::getColorFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pCameraX; cPoint.y = pCameraY; cPoint.z = pCameraZ;

			mInPoints3D.clear();
			mInPoints3D.push_back(cPoint);
			mOutPoints2D.clear();
			mOutPoints2D.resize(2);
			mCoordinateMapper->ProjectCameraToColor(1, &mInPoints3D[0], &mOutPoints2D[0]);

			int imageX = static_cast<int>(mOutPoints2D[0].x);
			int imageY = static_cast<int>(mOutPoints2D[0].y);
			if ((imageX >= 0 && imageX<mRgbSize.x) && (imageY >= 0 && imageY<mRgbSize.y))
				return mRgbFrame.getColor(imageX, imageY);
			return ofColor::black;
		}
		return ofColor::black;
	}

	const ofColor RSDevice::getColorFromDepthSpace(ofPoint pCameraPoint)
	{
		if (mCoordinateMapper)
			return getColorFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
		return ofColor::black;
	}

	//get ofColor space UVs from a depth image point
	const ofVec2f RSDevice::getColorCoordsFromDepthImage(float pImageX, float pImageY, float pImageZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pImageX;
			cPoint.y = pImageY;
			cPoint.z = pImageZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoints = new PXCPointF32[1];
			mCoordinateMapper->MapDepthToColor(1, cInPoint, cOutPoints);

			float cColorX = cOutPoints[0].x;
			float cColorY = cOutPoints[0].y;

			delete cInPoint;
			delete cOutPoints;
			return ofVec2f(cColorX / (float)mRgbSize.x, cColorY / (float)mRgbSize.y);
		}
		return ofVec2f(0);
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthImage(int pImageX, int pImageY, uint16_t pImageZ)
	{
		return getColorCoordsFromDepthImage(static_cast<float>(pImageX), static_cast<float>(pImageY), static_cast<float>(pImageZ));
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthImage(ofPoint pImageCoords)
	{
		return getColorCoordsFromDepthImage(pImageCoords.x, pImageCoords.y, pImageCoords.z);
	}

	//get ofColor space UVs from a depth space point
	const ofVec2f RSDevice::getColorCoordsFromDepthSpace(float pCameraX, float pCameraY, float pCameraZ)
	{
		if (mCoordinateMapper)
		{
			PXCPoint3DF32 cPoint;
			cPoint.x = pCameraX; cPoint.y = pCameraY; cPoint.z = pCameraZ;

			PXCPoint3DF32 *cInPoint = new PXCPoint3DF32[1];
			cInPoint[0] = cPoint;
			PXCPointF32 *cOutPoint = new PXCPointF32[1];
			mCoordinateMapper->ProjectCameraToColor(1, cInPoint, cOutPoint);

			ofVec2f cRetPt(cOutPoint[0].x / static_cast<float>(mRgbSize.x), cOutPoint[0].y / static_cast<float>(mRgbSize.y));
			delete cInPoint;
			delete cOutPoint;
			return cRetPt;
		}
		return ofVec2f(0);
	}

	const ofVec2f RSDevice::getColorCoordsFromDepthSpace(ofPoint pCameraPoint)
	{
		return getColorCoordsFromDepthSpace(pCameraPoint.x, pCameraPoint.y, pCameraPoint.z);
	}
}
#pragma endregion