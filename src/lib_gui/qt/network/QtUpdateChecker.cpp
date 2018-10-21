#include "QtUpdateChecker.h"

#include <QDesktopServices>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>

#include "LicenseChecker.h"
#include "QtRequest.h"
#include "ApplicationSettings.h"
#include "Version.h"
#include "utility.h"
#include "utilityApp.h"
#include "utilityUuid.h"

bool QtUpdateChecker::needsAutomaticCheck()
{
	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	return TimeStamp::now().deltaHours(appSettings->getLastUpdateCheck()) >= 24;
}

void QtUpdateChecker::check(bool force, std::function<void(Result)> callback)
{
	Result result;

	if (!force && !needsAutomaticCheck())
	{
		callback(result);
		return;
	}

	ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
	appSettings->setLastUpdateCheck(TimeStamp::now());
	appSettings->setUpdateVersion(Version::getApplicationVersion());
	appSettings->setUpdateDownloadUrl("");
	appSettings->save();

	QString urlString = "https://www.sourcetrail.com/api/v2/versions/latest";

	// OS
	std::string osString = utility::getOsTypeString();
	urlString += ("?os=" + osString).c_str();

	// architecture
	std::string platformString = (utility::getApplicationArchitectureType() == APPLICATION_ARCHITECTURE_X86_64 ? "64" : "32");
	urlString += ("&platform=" + platformString + "bit").c_str();

	// version
	// Version::setApplicationVersion(Version::fromString("2017.3.48")); // for debugging
	urlString += ("&version=" + Version::getApplicationVersion().toDisplayString()).c_str();

	// license
	std::string licenseString = LicenseChecker::getCurrentLicenseTypeString();
	urlString += ("&license=" + licenseString).c_str();

	// user token
	std::string token = appSettings->getUserToken();
	if (!token.size())
	{
		token = utility::getUuidString();
		appSettings->setUserToken(token);
		appSettings->save();
	}

	urlString += ("&token=" + token).c_str();

	// send request
	QtRequest* request = new QtRequest();
	QObject::connect(request, &QtRequest::receivedData,
		[force, callback, request](QByteArray bytes)
		{
			Result result;

			ApplicationSettings* appSettings = ApplicationSettings::getInstance().get();
			bool saveAppSettings = false;

			do
			{
				QJsonParseError error;
				QJsonDocument doc = QJsonDocument::fromJson(bytes, &error);
				if (doc.isNull() || !doc.isObject())
				{
					LOG_ERROR_STREAM(<< "Update response couldn't be parsed as JSON: " << error.errorString().toStdString());
					break;
				}

				QString news = doc.object().find("news")->toString();
				if (news.toStdString() != appSettings->getUpdateNews())
				{
					appSettings->setUpdateNews(news.toStdString());
					saveAppSettings = true;
				}


				QString version = doc.object().find("version")->toString();
				QString url = doc.object().find("url")->toString();

				Version updateVersion = Version::fromString(version.toStdString());
				if (!updateVersion.isValid())
				{
					LOG_ERROR_STREAM(<< "update version string is not valid: " << version.toStdString());
					break;
				}

				result.success = true;

				if (updateVersion > Version::getApplicationVersion())
				{
					result.version = updateVersion;
					result.url = url;

					appSettings->setUpdateVersion(updateVersion);
					appSettings->setUpdateDownloadUrl(url.toStdString());
					saveAppSettings = true;

					if (!force && appSettings->getSkipUpdateForVersion() == updateVersion)
					{
						break;
					}

					QMessageBox msgBox;
					msgBox.setText("Update Check");
					msgBox.setInformativeText(
						"Sourcetrail " + version + " is available for download: <a href=\"" + url + "\">" + url + "</a>");
					msgBox.addButton("Close", QMessageBox::ButtonRole::NoRole);
					msgBox.addButton("Skip this Version", QMessageBox::ButtonRole::NoRole);
					QPushButton* but = msgBox.addButton("Download", QMessageBox::ButtonRole::YesRole);
					msgBox.setDefaultButton(but);

					int val = msgBox.exec();

					if (val == 1)
					{
						appSettings->setSkipUpdateForVersion(updateVersion);
					}
					else if (val == 2)
					{
						QDesktopServices::openUrl(QUrl(url, QUrl::TolerantMode));
					}
				}
			}
			while (false);

			if (saveAppSettings)
			{
				appSettings->save();
			}

			request->deleteLater();

			callback(result);
		}
	);

	request->sendRequest(urlString);
}

void QtUpdateChecker::checkUpdate()
{
	m_onQtThread(
		[]()
		{
			check(false, [](Result){});
		}
	);
}
