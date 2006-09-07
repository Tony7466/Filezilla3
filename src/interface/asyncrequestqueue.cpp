#include "FileZilla.h"
#include "asyncrequestqueue.h"
#include "fileexistsdlg.h"
#include "Mainfrm.h"
#include "defaultfileexistsdlg.h"
#include "Options.h"

CAsyncRequestQueue::CAsyncRequestQueue(CMainFrame *pMainFrame)
{
	m_pMainFrame = pMainFrame;
}

CAsyncRequestQueue::~CAsyncRequestQueue()
{
}

bool CAsyncRequestQueue::ProcessDefaults(CFileZillaEngine *pEngine, CAsyncRequestNotification *pNotification)
{
	// Process notifications, see if we have defaults not requirering user interaction.
	switch (pNotification->GetRequestID())
	{
	case reqId_fileexists:
		{
			CFileExistsNotification *pFileExistsNotification = reinterpret_cast<CFileExistsNotification *>(pNotification);
			int action = pFileExistsNotification->overwriteAction;
			if (action != -1)
				action++;
			else
				action = CDefaultFileExistsDlg::GetDefault(pFileExistsNotification->download);
			if (action == -1)
				action = COptions::Get()->GetOptionVal(pFileExistsNotification->download ? OPTION_FILEEXISTS_DOWNLOAD : OPTION_FILEEXISTS_UPLOAD);

			// Ask and rename options require user interaction
			if (!action || action == 4)
				break;

			switch (action)
			{
			default:
				pFileExistsNotification->overwriteAction = CFileExistsNotification::overwrite;
				break;
			case 2:
				pFileExistsNotification->overwriteAction = CFileExistsNotification::overwriteNewer;
				break;
			case 3:
				pFileExistsNotification->overwriteAction = CFileExistsNotification::resume;
				break;
			case 5:
				pFileExistsNotification->overwriteAction = CFileExistsNotification::skip;
				break;
			}

			pEngine->SetAsyncRequestReply(pNotification);
			delete pNotification;

			return true;
		}
	default:
		break;
	}

	return false;
}

void CAsyncRequestQueue::AddRequest(CFileZillaEngine *pEngine, CAsyncRequestNotification *pNotification)
{
	ClearPending(pEngine);

	if (ProcessDefaults(pEngine, pNotification))
		return;

	t_queueEntry entry;
	entry.pEngine = pEngine;
	entry.pNotification = pNotification;

	m_requestList.push_back(entry);

	if (m_requestList.size() == 1)
		ProcessNextRequest();
}

void CAsyncRequestQueue::ProcessNextRequest()
{
	while (!m_requestList.empty())
	{
		t_queueEntry &entry = m_requestList.front();

		if (!entry.pEngine->IsPendingAsyncRequestReply(entry.pNotification))
		{
			delete entry.pNotification;
			m_requestList.pop_front();
			continue;
		}
		
		if (entry.pNotification->GetRequestID() == reqId_fileexists)
		{
			CFileExistsNotification *pNotification = reinterpret_cast<CFileExistsNotification *>(entry.pNotification);

			int action = pNotification->overwriteAction;
			if (action != -1)
				action++;
			else
				action = CDefaultFileExistsDlg::GetDefault(pNotification->download);
			if (action == -1)
				action = COptions::Get()->GetOptionVal(pNotification->download ? OPTION_FILEEXISTS_DOWNLOAD : OPTION_FILEEXISTS_UPLOAD);

			if (!action)
			{
				CFileExistsDlg dlg(pNotification);
				dlg.Create(m_pMainFrame);
				int res = dlg.ShowModal();

				if (res == wxID_OK)
				{
					action = dlg.GetAction() + 1;

					bool directionOnly, queueOnly;
					if (dlg.Always(directionOnly, queueOnly))
					{
						if (!queueOnly)
						{
							if (pNotification->download || !directionOnly)
								CDefaultFileExistsDlg::SetDefault(true, action);

							if (!pNotification->download || !directionOnly)
								CDefaultFileExistsDlg::SetDefault(false, action);
						}
						else
						{
							// For the notification already in the request queue, we have to set the queue action directly
							for (std::list<t_queueEntry>::iterator iter = m_requestList.begin()++; iter != m_requestList.end(); iter++)
							{
								if (pNotification->GetRequestID() != reqId_fileexists)
									continue;

								reinterpret_cast<CFileExistsNotification *>(entry.pNotification)->overwriteAction = CFileExistsNotification::OverwriteAction(action - 1);
							}
		
							// Todo: Pass value to queue
						}
					}
				}
				else
					action = 5;
			}

			if (action < 1 || action > 5)
				action = 5;

			switch (action)
			{
			case 1:
				pNotification->overwriteAction = CFileExistsNotification::overwrite;
				break;
			case 2:
				pNotification->overwriteAction = CFileExistsNotification::overwriteNewer;
				break;
			case 3:
				pNotification->overwriteAction = CFileExistsNotification::resume;
				break;
			case 4:
				{
					wxString msg;
					wxString defaultName;
					if (pNotification->download)
					{
						msg.Printf(_("The file %s already exists.\nPlease enter a new name:"), pNotification->localFile.c_str());
						wxFileName fn = pNotification->localFile;
						defaultName = fn.GetFullName();
					}
					else
					{
						wxString fullName = pNotification->remotePath.GetPath() + pNotification->remoteFile;
						msg.Printf(_("The file %s already exists.\nPlease enter a new name:"), fullName.c_str());
						defaultName = pNotification->remoteFile;
					}
					wxTextEntryDialog dlg(m_pMainFrame, msg, _("Rename file"), defaultName);

					// Repeat until user cancels or entery a new name
					while (1)
					{
						int res = dlg.ShowModal();
						if (res == wxID_OK)
						{
							if (dlg.GetValue() == _T(""))
								continue; // Disallow empty names
							if (dlg.GetValue() == defaultName)
							{
								wxMessageDialog dlg2(m_pMainFrame, _("You did not enter a new name for the file. Overwrite the file instead?"), _("Filename unchanged"), 
									wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION | wxCANCEL);
								int res = dlg2.ShowModal();

								if (res == wxID_CANCEL)
									pNotification->overwriteAction = CFileExistsNotification::skip;
								else if (res == wxID_NO)
									continue;
								else
									pNotification->overwriteAction = CFileExistsNotification::skip;
							}
							else
							{
								pNotification->overwriteAction = CFileExistsNotification::rename;
								pNotification->newName = dlg.GetValue();
							}
						}
						else
							pNotification->overwriteAction = CFileExistsNotification::skip;
						break;
					}
				}
				break;
			default:
				pNotification->overwriteAction = CFileExistsNotification::skip;
				break;
			}

			entry.pEngine->SetAsyncRequestReply(entry.pNotification);
			delete pNotification;
		}
		else if (entry.pNotification->GetRequestID() == reqId_interactiveLogin)
		{
			CInteractiveLoginNotification* pNotification = reinterpret_cast<CInteractiveLoginNotification*>(entry.pNotification);

			if (m_pMainFrame->GetPassword(pNotification->server, _T(""), pNotification->challenge))
				pNotification->passwordSet = true;

			entry.pEngine->SetAsyncRequestReply(pNotification);
			delete pNotification;
		}
		else if (entry.pNotification->GetRequestID() == reqId_hostkey || entry.pNotification->GetRequestID() == reqId_hostkeyChanged)
		{
			CHostKeyNotification *pNotification = reinterpret_cast<CHostKeyNotification *>(entry.pNotification);

			wxDialogEx* pDlg = new wxDialogEx;
			if (pNotification->GetRequestID() == reqId_hostkey)
				pDlg->Load(m_pMainFrame, _T("ID_HOSTKEY"));
			else
				pDlg->Load(m_pMainFrame, _T("ID_HOSTKEYCHANGED"));

			pDlg->WrapText(pDlg, XRCID("ID_DESC"), 400);

			pDlg->SetLabel(XRCID("ID_HOST"), wxString::Format(_T("%s:%d"), pNotification->GetHost().c_str(), pNotification->GetPort()));
			pDlg->SetLabel(XRCID("ID_FINGERPRINT"), pNotification->GetFingerprint());

			pDlg->GetSizer()->Fit(pDlg);
			pDlg->GetSizer()->SetSizeHints(pDlg);

			int res = pDlg->ShowModal();

			if (res == wxID_OK)
			{
				pNotification->m_trust = true;
				pNotification->m_alwaysTrust = XRCCTRL(*pDlg, "ID_ALWAYS", wxCheckBox)->GetValue();
			}
			
			entry.pEngine->SetAsyncRequestReply(pNotification);
			delete pNotification;
		}
		

		RecheckDefaults();
		m_requestList.pop_front();
	}
}

void CAsyncRequestQueue::ClearPending(const CFileZillaEngine *pEngine)
{
	if (m_requestList.empty())
		return;
	
	// Remove older requests coming from the same engine, but never the first
	// entry in the list as that one displays a dialog at this moment.
	for (std::list<t_queueEntry>::iterator iter = m_requestList.begin()++; iter != m_requestList.end(); iter++)
	{
		if (iter->pEngine == pEngine)
		{
			m_requestList.erase(iter);

			// At most one pending request per engine
			break;
		}
	}
}

void CAsyncRequestQueue::RecheckDefaults()
{
	if (m_requestList.size() <= 1)
		return;

	std::list<t_queueEntry>::iterator cur, next;
	cur = ++m_requestList.begin();
	while (cur != m_requestList.end())
	{
		next = cur;
		next++;

		if (ProcessDefaults(cur->pEngine, cur->pNotification))
			m_requestList.erase(cur);
		cur = next;
	}
}
