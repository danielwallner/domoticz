#pragma once

#include "DomoticzHardware.h"

#include <string>
#include <boost/asio.hpp>

class CKodiNode : public std::enable_shared_from_this<CKodiNode>, StoppableTask
{
	class CKodiStatus
	{
	      public:
		CKodiStatus()
		{
			Clear();
		};
		_eMediaStatus Status()
		{
			return m_nStatus;
		};
		_eNotificationTypes NotificationType();
		std::string StatusText()
		{
			return Media_Player_States(m_nStatus);
		};
		void Status(_eMediaStatus pStatus)
		{
			m_nStatus = pStatus;
		};
		void Status(const std::string &pStatus)
		{
			m_sStatus = pStatus;
		};
		void PlayerID(int pPlayerID)
		{
			m_iPlayerID = pPlayerID;
		};
		std::string PlayerID()
		{
			if (m_iPlayerID >= 0)
				return std::to_string(m_iPlayerID);
			return "";
		};
		void Type(const char *pType)
		{
			m_sType = pType;
		};
		std::string Type()
		{
			return m_sType;
		};
		void Title(const char *pTitle)
		{
			m_sTitle = pTitle;
		};
		void ShowTitle(const char *pShowTitle)
		{
			m_sShowTitle = pShowTitle;
		};
		void Artist(const char *pArtist)
		{
			m_sArtist = pArtist;
		};
		void Album(const char *pAlbum)
		{
			m_sAlbum = pAlbum;
		};
		void Channel(const char *pChannel)
		{
			m_sChannel = pChannel;
		};
		void Season(int pSeason)
		{
			m_iSeason = pSeason;
		};
		void Episode(int pEpisode)
		{
			m_iEpisode = pEpisode;
		};
		void Label(const char *pLabel)
		{
			m_sLabel = pLabel;
		};
		void Percent(float fPercent)
		{
			m_sPercent = "";
			if (fPercent > 1.0)
				m_sPercent = std::to_string((int)round(fPercent)) + "%";
		};
		void Year(int pYear)
		{
			m_sYear = std::to_string(pYear);
			if (m_sYear.length() > 2)
				m_sYear = "(" + m_sYear + ")";
			else
				m_sYear = "";
		};
		void Live(bool pLive)
		{
			m_sLive = "";
			if (pLive)
				m_sLive = "(Live)";
		};
		void LastOK(time_t pLastOK)
		{
			m_tLastOK = pLastOK;
		};
		std::string LastOK()
		{
			std::string sRetVal;
			tm ltime;
			localtime_r(&m_tLastOK, &ltime);
			char szLastUpdate[40];
			sprintf(szLastUpdate, "%04d-%02d-%02d %02d:%02d:%02d", ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday, ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
			sRetVal = szLastUpdate;
			return sRetVal;
		};
		void Clear();
		std::string LogMessage();
		std::string StatusMessage();
		bool LogRequired(CKodiStatus &);
		bool UpdateRequired(CKodiStatus &);
		bool OnOffRequired(CKodiStatus &);
		bool IsOn()
		{
			return (m_nStatus != MSTAT_OFF);
		};
		bool IsStreaming()
		{
			return ((m_nStatus == MSTAT_VIDEO) || (m_nStatus == MSTAT_AUDIO) || (m_nStatus == MSTAT_PAUSED) || (m_nStatus == MSTAT_PHOTO));
		};

	      private:
		_eMediaStatus m_nStatus;
		std::string m_sStatus;
		int m_iPlayerID;
		std::string m_sType;
		std::string m_sShowTitle;
		std::string m_sTitle;
		std::string m_sArtist;
		std::string m_sAlbum;
		std::string m_sChannel;
		int m_iSeason;
		int m_iEpisode;
		std::string m_sLabel;
		std::string m_sPercent;
		std::string m_sYear;
		std::string m_sLive;
		time_t m_tLastOK;
	};

      public:
	CKodiNode(boost::asio::io_context *, int, int, int, const std::string &, const std::string &, const std::string &, const std::string &);
	~CKodiNode();
	void Do_Work();
	void SendCommand(const std::string &);
	void SendCommand(const std::string &, int iValue);
	void SetPlaylist(const std::string &playlist);
	void SetExecuteCommand(const std::string &command);
	bool SendShutdown();
	void StopRequest()
	{
		RequestStop();
	};
	bool IsBusy()
	{
		return m_Busy;
	};
	bool IsOn()
	{
		return (m_CurrentStatus.Status() != MSTAT_OFF);
	};

	int m_ID;
	int m_DevID;
	std::string m_Name;

      protected:
	bool m_Busy;
	bool m_Stoppable;

      private:
	void handleConnect();
	void handleRead(const boost::system::error_code &, std::size_t);
	void handleWrite(const std::string &);
	void handleDisconnect();
	void handleMessage(std::string &);

	int m_HwdID;
	char m_szDevID[40];
	std::string m_IP;
	std::string m_Port;

	CKodiStatus m_PreviousStatus;
	CKodiStatus m_CurrentStatus;
	void UpdateStatus();

	std::string m_PlaylistType;
	std::string m_Playlist;
	int m_PlaylistPosition;

	std::string m_ExecuteCommand;

	std::string m_RetainedData;

	int m_iTimeoutCnt;
	int m_iPollIntSec;
	int m_iMissedPongs;
	std::string m_sLastMessage;
	boost::asio::io_context *m_Ioc;
	boost::asio::ip::tcp::socket *m_Socket;
	std::array<char, 256> m_Buffer;
};

class CKodi : public CDomoticzHardwareBase
{
      public:
	CKodi(int ID, int PollIntervalsec, int PingTimeoutms);
	explicit CKodi(int ID);
	~CKodi() override;
	bool WriteToHardware(const char *pdata, unsigned char length) override;
	void AddNode(const std::string &Name, const std::string &IPAddress, int Port);
	bool UpdateNode(int ID, const std::string &Name, const std::string &IPAddress, int Port);
	void RemoveNode(int ID);
	void RemoveAllNodes();
	void SetSettings(int PollIntervalsec, int PingTimeoutms);
	void SendCommand(int ID, const std::string &command);
	bool SetPlaylist(int ID, const std::string &playlist);
	bool SetExecuteCommand(int ID, const std::string &command);

      private:
	void Do_Work();

	bool StartHardware() override;
	bool StopHardware() override;

	void ReloadNodes();
	void UnloadNodes();

      private:
	static std::vector<std::shared_ptr<CKodiNode>> m_pNodes;
	int m_iPollInterval;
	int m_iPingTimeoutms;
	std::shared_ptr<std::thread> m_thread;
	std::mutex m_mutex;
	boost::asio::io_context m_ioc;
};
