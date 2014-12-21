#include "FileManager.h"
#include "wx/event.h"
#include "PlayerFrame.h"
#include "wx/log.h"

ListUpdateEv::ListUpdateEv(File file) : wxCommandEvent(EVT_SEARCHER_UPDATE),
	mFile(file)	
	{}

void FileManager::AddLib(MediaLibrary lib)
{
	if (FindLib(lib.GetName()) != -1)
		throw MyException("There already exists library with this name",
				MyException::NOT_FATAL);
	mLibs.push_back(lib);
}

void FileManager::AddLib(const wxString & name)
{
	if (FindLib(name) != -1)
		throw MyException("There already exist library with this name",
				MyException::NOT_FATAL);
	mLibs.push_back(name);
}

void FileManager::AddLibs(MediaLibrary libs[], int n)
{
	for (int i = 0; i < n; i++)
	{
		AddLib(libs[i]);
	}
}

int FileManager::FindLib(const wxString & name)
{
	for (int i = 0; i < mLibs.size(); i++)
	{
		if (name == mLibs[i].GetName())
			return i;
	}
	return -1;
}

const Playlist * FileManager::GetPlaylist(const wxString & libName, 
		const wxString & playlistName)
{
	int ind = FindLib(libName);
	assert(ind > -1);
	return mLibs[ind].GetPlaylist(playlistName);
}

void FileManager::Search(wxString libName, wxString playlistName)
{
	mCurrLib = libName;
	mCurrPlaylist = playlistName;	
	mpThread = new SearcherThread(this, mHandlerFrame);
   	if ( mpThread->Run() != wxTHREAD_NO_ERROR )
	{
		delete mpThread;
		mpThread = NULL;
		throw MyException("Failed to run searcher thread!", 
				MyException::FATAL_ERROR);
	}
}

int FileManager::FromLib(const File & file)
{
	wxString ext = file.GetExt();
	for (int i = 0; i < mLibs.size(); i++)
	{
		if (mLibs[i].IsRightExtension(ext))
		{
			return i;
		}
	}
	return -1;
}


wxThread::ExitCode FileManager::SearcherThread::Entry()
{
	mDir.Open(mFirstDir);
	int n = mDir.Traverse(*this, wxEmptyString, wxDIR_DIRS | wxDIR_FILES);
	if (n == -1)
		throw MyException("wxDir::Traverse failed", 
			MyException::FATAL_ERROR);
	mSearchStage++;
#ifndef __LINUX__
	mDir.Open(mSecondDir);
	n = mDir.Traverse(*this, wxEmptyString, wxDIR_DIRS | wxDIR_FILES);
	if (n == -1)
		throw MyException("wxDir::Traverse failed", 
			MyException::FATAL_ERROR);
#endif

	if (!mStopped)
	{
		wxQueueEvent(mHandlerFrame, 
			new wxThreadEvent(EVT_SEARCHER_COMPLETE));
	}

	 return (wxThread::ExitCode)0; // success
}

wxDirTraverseResult 
	FileManager::SearcherThread::OnFile(const wxString& filename)
{
	if (!TestDestroy())
	{
		wxCriticalSectionLocker enter(mFManagerCS);
		wxMessageOutputDebug().Printf(filename);
		File file(filename);
		//TODO add function - FromPlaylist
		int ind = mFManager->FromLib(file);
		if (ind > -1)
		{
			mFManager->mFiles.push_back(file);
			mFManager->mLibs[ind].AddFile(&mFManager->mFiles.back());
			wxQueueEvent(mHandlerFrame, new ListUpdateEv(file));
		}
		return wxDIR_CONTINUE;
	}
	else
	{
		mStopped = true;	
		return wxDIR_STOP;
	}
}
wxDirTraverseResult 
	FileManager::SearcherThread::OnDir(const wxString& dirname)
{
	if (!TestDestroy())
	{
		if (mSearchStage > 0 && dirname == mFirstDir)
			return wxDIR_IGNORE;
		else
			return wxDIR_CONTINUE;
	}
	else
		return wxDIR_STOP;
}

FileManager::SearcherThread::~SearcherThread()
{
	 wxCriticalSectionLocker enter(mFManager->mThreadCS);
	// the thread is being destroyed; make sure not to leave dangling pointers around
	mFManager->mpThread = NULL;
}

void FileManager::StopSearch()
{
	{
		wxCriticalSectionLocker enter(mThreadCS);
		if (mpThread) // does the thread still exist?
		{
			wxMessageOutputDebug().Printf("FileManager: deleting thread");
			if (mpThread->Delete() != wxTHREAD_NO_ERROR )
				wxLogError("Can't delete searcher thread!");
		}
	} // exit from the critical section to give the thread
 	// the possibility to enter its destructor
	// (which is guarded with mpThreadCS critical section!)
	while (1)
	{
		{ // was the ~MyThread() function executed?
			wxCriticalSectionLocker enter(mThreadCS);
			if (!mpThread) break;
		}
		// wait for thread completion
		wxThread::This()->Sleep(1);
	}
}

FileManager::SearcherThread::SearcherThread(
		FileManager * fMan, PlayerFrame *handler)
		: wxThread(wxTHREAD_DETACHED), wxDirTraverser(), mSearchStage(0),
		mFManager(fMan), mStopped(false)
{ 
	mHandlerFrame = handler;
#ifdef __LINUX__
	mFirstDir = "/home";
	mSecondDir = "/";
#endif
#ifdef __WINDOWS__
	mFirstDir = "/Users";
	mSecondDir = "/";
#endif
}
