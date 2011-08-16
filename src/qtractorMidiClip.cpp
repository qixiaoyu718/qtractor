// qtractorMidiClip.cpp
//
/****************************************************************************
   Copyright (C) 2005-2011, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorAbout.h"
#include "qtractorMidiClip.h"
#include "qtractorMidiEngine.h"

#include "qtractorSession.h"

#include "qtractorDocument.h"

#include "qtractorMidiEditor.h"
#include "qtractorMidiEditorForm.h"

#include "qtractorMainForm.h"

#ifdef QTRACTOR_MIDI_EDITOR_TOOL
#include "qtractorOptions.h"
#endif

#include <QFileInfo>
#include <QPainter>

#include <QDomDocument>

#if QT_VERSION < 0x040300
#define lighter(x) light(x)
#endif

#if QT_VERSION < 0x040500
namespace Qt {
const WindowFlags WindowCloseButtonHint = WindowFlags(0x08000000);
#if QT_VERSION < 0x040200
const WindowFlags CustomizeWindowHint   = WindowFlags(0x02000000);
#endif
}
#endif


//----------------------------------------------------------------------
// class qtractorMidiClip -- MIDI sequemce clip.
//

// Constructor.
qtractorMidiClip::qtractorMidiClip ( qtractorTrack *pTrack )
	: qtractorClip(pTrack)
{
	m_pFile = NULL;
	m_pSeq  = NULL;

	m_iTrackChannel = 0;
	m_iFormat = defaultFormat();
	m_bSessionFlag = false;
	m_iRevision = 0;

	m_noteMin = 0;
	m_noteMax = 0;

	m_pMidiEditorForm = NULL;
}

// Copy constructor.
qtractorMidiClip::qtractorMidiClip ( const qtractorMidiClip& clip )
	: qtractorClip(clip.track())
{
	m_pFile = NULL;
	m_pSeq  = NULL;

	setFilename(clip.filename());
	setTrackChannel(clip.trackChannel());
	setClipGain(clip.clipGain());

	m_iFormat = clip.format();
	m_bSessionFlag = false;
	m_iRevision = 0;

	m_noteMin = clip.noteMin();
	m_noteMax = clip.noteMax();

	m_pMidiEditorForm = NULL;
}


// Destructor.
qtractorMidiClip::~qtractorMidiClip (void)
{
	close();

	closeMidiFile();
}


// Brand new clip contents new method.
bool qtractorMidiClip::createMidiFile (
	const QString& sFilename, int iTrackChannel )
{
	closeMidiFile();

	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return false;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return false;

#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::createMidiFile(\"%s\", %d)", this,
		sFilename.toUtf8().constData(), iTrackChannel);
#endif

	// Create and open up the MIDI file...
	m_pFile = new qtractorMidiFile();
	if (!m_pFile->open(sFilename, qtractorMidiFile::Write)) {
		delete m_pFile;
		m_pFile = NULL;
		return false;
	}

	// Self holds the SMF format,
	unsigned short iFormat = format();
	unsigned short iTracks = 1;

	// Which SMF format?
	if (iFormat == 0) {
		// SMF format 0 (1 track, 1 channel)
		iTrackChannel = pTrack->midiChannel();
	} else {
		// SMF format 1 (2 tracks, 1 channel)
		iTrackChannel = 1;
		++iTracks;
	}

	// Set local properties...
	setFilename(m_pFile->filename());
	setTrackChannel(iTrackChannel);
	setDirty(false);

	// Initialize MIDI event container...
	m_pSeq = new qtractorMidiSequence();

	m_pSeq->setName(QFileInfo(m_pFile->filename()).baseName());
	m_pSeq->setChannel(pTrack->midiChannel());
	m_pSeq->setTicksPerBeat(pSession->ticksPerBeat());

	// Make it a brand new revision...
	setRevision(1);

	// Write SMF header...
	if (m_pFile->writeHeader(iFormat, iTracks, m_pSeq->ticksPerBeat())) {
		// Set initial local properties...
		if (m_pFile->tempoMap()) {
			m_pFile->tempoMap()->fromTimeScale(
				pSession->timeScale(),
				pSession->tickFromFrame(clipStart()));
		}
		// Sure this is a brand new file...
		if (iFormat == 1)
			m_pFile->writeTrack(NULL);
		m_pFile->writeTrack(m_pSeq);
		m_pFile->close();
	}

	// It's there now.
	delete m_pFile;
	m_pFile = NULL;

	// Clip name should be clear about it all.
	if (clipName().isEmpty())
		setClipName(m_pSeq->name());
	if (clipName().isEmpty())
		setClipName(QFileInfo(filename()).baseName());

	// Uh oh...
	m_playCursor.reset(m_pSeq);
	m_drawCursor.reset(m_pSeq);

	return true;
}


// The main use method.
bool qtractorMidiClip::openMidiFile (
	const QString& sFilename, int iTrackChannel, int iMode )
{
	closeMidiFile();

	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return false;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return false;

#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::openMidiFile(\"%s\", %d, %d)", this,
		sFilename.toUtf8().constData(), iTrackChannel, iMode);
#endif

	// Create and open up the MIDI file...
	m_pFile = new qtractorMidiFile();
	if (!m_pFile->open(sFilename, iMode)) {
		delete m_pFile;
		m_pFile = NULL;
		return false;
	}

	// Set local properties...
	setFilename(m_pFile->filename());
	setTrackChannel(iTrackChannel);
	setDirty(false);

	// New data sequence holder...
	m_pSeq = new qtractorMidiSequence();

	m_pSeq->clear();
	m_pSeq->setTicksPerBeat(pSession->ticksPerBeat());

	qtractorTimeScale::Cursor cursor(pSession->timeScale());
	qtractorTimeScale::Node *pNode = cursor.seekFrame(clipStart());
	unsigned long t0 = pNode->tickFromFrame(clipStart());

	if (clipStart() > clipOffset()) {
		unsigned long iOffset = clipStart() - clipOffset();
		pNode = cursor.seekFrame(iOffset);
		m_pSeq->setTimeOffset(t0 - pNode->tickFromFrame(iOffset));
	} else {
		pNode = cursor.seekFrame(clipOffset());
		m_pSeq->setTimeOffset(pNode->tickFromFrame(clipOffset()));
	}

	unsigned long iLength = clipStart() + clipLength();
	pNode = cursor.seekFrame(iLength);
	m_pSeq->setTimeLength(pNode->tickFromFrame(iLength) - t0);

	// Initial statistics...
	m_pSeq->setNoteMin(m_noteMin);
	m_pSeq->setNoteMax(m_noteMax);

	// Are we on a pre-writing status?
	if (m_pFile->mode() == qtractorMidiFile::Write) {
		// On write mode, iTrackChannel holds the SMF format,
		// so we'll convert it here as properly.
		unsigned short iFormat = 0;
		unsigned short iTracks = 1;
		if (iTrackChannel == 0) {
			// SMF format 0 (1 track, 1 channel)
			iTrackChannel = pTrack->midiChannel();
		} else {
			// SMF format 1 (2 tracks, 1 channel)
			iTrackChannel = 1;
			iFormat = 1;
			++iTracks;
		}
		// That's it.
		setFormat(iFormat);
		// Write SMF header...
		if (m_pFile->writeHeader(iFormat, iTracks, m_pSeq->ticksPerBeat())) {
			// Set initial local properties...
			if (m_pFile->tempoMap()) {
				m_pFile->tempoMap()->fromTimeScale(
					pSession->timeScale(), m_pSeq->timeOffset());
			}
		}
		// And initial clip name...
		m_pSeq->setName(QFileInfo(m_pFile->filename()).baseName());
		m_pSeq->setChannel(pTrack->midiChannel());
		// Nothing more as for writing...
	} else {
		// On read mode, SMF format is properly given by open file.
		setFormat(m_pFile->format());
		// Read the event sequence in...
		m_pFile->readTrack(m_pSeq, iTrackChannel);
		// For immediate feedback, once...
		m_noteMin = m_pSeq->noteMin();
		m_noteMax = m_pSeq->noteMax();
		// FIXME: On demand, set session time properties from MIDI file...
		if (m_bSessionFlag) {
		#if 0
			// Import eventual SysEx setup...
			// - take care that given track might not be currently open,
			//   so that we'll resolve MIDI output bus somehow...
			qtractorMidiBus *pMidiBus = NULL;
			qtractorMidiEngine *pMidiEngine = pSession->midiEngine();
			if (pMidiEngine) {
				pMidiBus = static_cast<qtractorMidiBus *> (
					pMidiEngine->findOutputBus(pTrack->outputBusName()));
				if (pMidiBus == NULL) {
					for (qtractorBus *pBus = pMidiEngine->buses().first();
							pBus; pBus = pBus->next()) {
						if (pBus->busMode() & qtractorBus::Output) {
							pMidiBus = static_cast<qtractorMidiBus *> (pBus);
							break;
						}
					}
				}
			}
			// Import eventual SysEx setup...
			if (pMidiBus)
				pMidiBus->importSysexList(m_pSeq);
		#endif
			// Import tempo map as well...
			if (m_pFile->tempoMap()) {
				m_pFile->tempoMap()->intoTimeScale(pSession->timeScale(), t0);
				pSession->updateTimeScale();
			}
			// Reset session flag now.
			m_bSessionFlag = false;
		}
		// We should have events, otherwise this clip is of no use...
		//if (m_pSeq->events().count() < 1)
		//	return false;
	}

	// Default clip length will be whole sequence duration.
	if (clipLength() == 0 && m_pSeq->timeLength() > m_pSeq->timeOffset()) {
		unsigned long t1 = t0 + (m_pSeq->timeLength() - m_pSeq->timeOffset());
		pNode = cursor.seekTick(t1);
		setClipLength(pNode->frameFromTick(t1) - clipStart());
	}

	// Clip name should be clear about it all.
	if (clipName().isEmpty())
		setClipName(m_pSeq->name());
	if (clipName().isEmpty())
		setClipName(QFileInfo(filename()).baseName());

	// Uh oh...
	m_playCursor.reset(m_pSeq);
	m_drawCursor.reset(m_pSeq);

	return true;
}


// Private cleanup.
void qtractorMidiClip::closeMidiFile (void)
{
	if (m_pMidiEditorForm) {
		m_pMidiEditorForm->close();
		delete m_pMidiEditorForm;
		m_pMidiEditorForm = NULL;
	}

	if (m_pSeq) {
		delete m_pSeq;
		m_pSeq = NULL;
	}

	if (m_pFile) {
		delete m_pFile;
		m_pFile = NULL;
	}
}


// Revisionist method.
QString qtractorMidiClip::createFilePathRevision ( bool bForce )
{
	QString sFilename = filename();

	if (m_iRevision == 0 || bForce) {
		qtractorSession *pSession = qtractorSession::getInstance();
		if (pSession && track())
			sFilename = pSession->createFilePath(track()->trackName(), "mid");
		sFilename = qtractorMidiFile::createFilePathRevision(sFilename);
	#ifdef CONFIG_DEBUG
		qDebug("qtractorMidiClip::createFilePathRevision(\"%s\")",
			sFilename.toUtf8().constData());
	#endif
	}

	if (!bForce) ++m_iRevision;

	return sFilename;
}


// Intra-clip playback frame positioning.
void qtractorMidiClip::seek ( unsigned long iFrame )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::seek(%lu)", this, iFrame);
#endif

	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return;

	qtractorMidiSequence *pSeq = sequence();
	if (pSeq == NULL)
		return;

	unsigned long t0 = pSession->tickFromFrame(clipStart());
	unsigned long t1 = pSession->tickFromFrame(iFrame);

	// Seek for the nearest sequence event...
	m_playCursor.seek(pSeq, (t1 > t0 ? t1 - t0 : 0));
}


// Reset clip state.
void qtractorMidiClip::reset ( bool bLooping )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::reset(%d)", this, (int) bLooping);
#endif

	qtractorMidiSequence *pSeq = sequence();
	if (pSeq == NULL)
		return;

	// Reset to the first sequence event...
	m_playCursor.reset(pSeq);

	// Take the time from loop-start?
	if (bLooping && clipLoopStart() < clipLoopEnd())
		seek(clipLoopStart());
}


// Loop positioning.
void qtractorMidiClip::set_loop ( unsigned long /* iLoopStart */,
	unsigned long /* iLoopEnd */ )
{
	// Do nothing?
}


// Clip close-commit (record specific)
void qtractorMidiClip::close (void)
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::close(%d)\n", this);
#endif

	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return;

	// Take pretended clip-length...
	unsigned long iClipLength = clipLength();
	if (m_pSeq) {
		if (iClipLength > 0)
			m_pSeq->setTimeLength(pSession->tickFromFrame(iClipLength));
		// Final read statistics...
		m_noteMin = m_pSeq->noteMin();
		m_noteMax = m_pSeq->noteMax();
		// Actual sequence closure...
		m_pSeq->close();
		// Commit the final clip length...
		if (iClipLength < 1) {
			iClipLength = pSession->frameFromTick(m_pSeq->duration());
			setClipLength(iClipLength);
		}
	}

	// Now's time to write the whole thing...
	bool bNewFile = (m_pFile && m_pFile->mode() == qtractorMidiFile::Write);
	if (bNewFile && iClipLength > 0 && m_pSeq) {
		// Write channel tracks...
		if (m_iFormat == 1)
			m_pFile->writeTrack(NULL);	// Setup track (SMF format 1).
		m_pFile->writeTrack(m_pSeq);	// Channel track.
		m_pFile->close();
	}

	// Just to be sure things get deallocated..
	closeMidiFile();

	// If proven empty, remove the file.
	if (bNewFile && iClipLength < 1)
		QFile::remove(filename());
}


// MIDI clip (re)open method.
void qtractorMidiClip::open (void)
{
	// Go open the proper file...
	openMidiFile(filename(), m_iTrackChannel);
}


// Audio clip special process cycle executive.
void qtractorMidiClip::process ( unsigned long iFrameStart,
	unsigned long iFrameEnd )
{
#ifdef CONFIG_DEBUG_0
	qDebug("qtractorMidiClip[%p]::process(%lu, %lu)\n",
		this, iFrameStart, iFrameEnd);
#endif

	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return;

	qtractorMidiEngine *pMidiEngine = pSession->midiEngine();
	if (pMidiEngine == NULL)
		return;

	qtractorMidiSequence *pSeq = sequence();
	if (pSeq == NULL)
		return;

	// Track mute state...
	bool bMute = (pTrack->isMute()
		|| (pSession->soloTracks() && !pTrack->isSolo()));

	unsigned long t0 = pSession->tickFromFrame(clipStart());

	unsigned long iTimeStart = pSession->tickFromFrame(iFrameStart);
	unsigned long iTimeEnd   = pSession->tickFromFrame(iFrameEnd);

	// Enqueue the requested events...
	qtractorMidiEvent *pEvent
		= m_playCursor.seek(pSeq, iTimeStart > t0 ? iTimeStart - t0 : 0);
	while (pEvent) {
		unsigned long t1 = t0 + pEvent->time();
		if (t1 >= iTimeEnd)
			break;
		if (t1 >= iTimeStart
			&& (!bMute || pEvent->type() != qtractorMidiEvent::NOTEON))
			pMidiEngine->enqueue(pTrack, pEvent, t1,
				gain(pSession->frameFromTick(t1) - clipStart()));
		pEvent = pEvent->next();
	}
}


// Audio clip paint method.
void qtractorMidiClip::draw ( QPainter *pPainter, const QRect& clipRect,
	unsigned long iClipOffset )
{
	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return;

	qtractorMidiSequence *pSeq = sequence();
	if (pSeq == NULL)
		return;

	// Check maximum note span...
	int iNoteSpan = (pSeq->noteMax() - pSeq->noteMin()) + 1;
	if (iNoteSpan < 4)
		iNoteSpan = 4;

	unsigned long iFrameStart = clipStart() + iClipOffset;
	int cx = pSession->pixelFromFrame(iFrameStart);

	qtractorTimeScale::Cursor cursor(pSession->timeScale());
	qtractorTimeScale::Node *pNode = cursor.seekFrame(clipStart());
	unsigned long t0 = pNode->tickFromFrame(clipStart());

	pNode = cursor.seekFrame(iFrameStart);	
	unsigned long iTimeStart = pNode->tickFromFrame(iFrameStart);
	pNode = cursor.seekPixel(cx + clipRect.width());	
	unsigned long iTimeEnd = pNode->tickFromPixel(cx + clipRect.width());

	const QColor& fg = track()->foreground();
	pPainter->setPen(fg);
	pPainter->setBrush(fg.lighter());

	bool bClipRecord = (track()->clipRecord() == this);
	int h1 = clipRect.height() - 3;
	int h  = h1 / iNoteSpan;
	if (h < 3) h = 3;

	qtractorMidiEvent *pEvent
		= m_drawCursor.reset(pSeq, iTimeStart > t0 ? iTimeStart - t0 : 0);
	while (pEvent) {
		unsigned long t1 = t0 + pEvent->time();
		if (t1 >= iTimeEnd)
			break;
		unsigned long t2 = t1 + pEvent->duration();
		if (pEvent->type() == qtractorMidiEvent::NOTEON && t2 >= iTimeStart) {
			pNode = cursor.seekTick(t1);
			int x = clipRect.x() + pNode->pixelFromTick(t1) - cx;
			int y = clipRect.bottom()
				- (h1 * (pEvent->note() - pSeq->noteMin() + 1)) / iNoteSpan;
			int w = (pEvent->duration() > 0 || !bClipRecord
				? clipRect.x() + pNode->pixelFromTick(t2) - cx
				: clipRect.right()) - x; // Pending note-off? (while recording)
			if (h > 3 && w > 3) {
			//	if (w < 5) w = 5;
				pPainter->fillRect(x, y, w, h - 1, fg);
				pPainter->fillRect(x + 1, y + 1, w - 4, h - 4, fg.lighter());
			} else {
				if (w < 3) w = 3;
				pPainter->drawRect(x, y, w, h - 1);
			}
		}
		pEvent = pEvent->next();
	}
}


// Clip editor method.
bool qtractorMidiClip::startEditor ( QWidget *pParent )
{
	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return false;

	if (m_pMidiEditorForm == NULL) {
		// Build up the editor form...
		// What style do we create tool childs?
		Qt::WindowFlags wflags = Qt::Window
			| Qt::CustomizeWindowHint
			| Qt::WindowTitleHint
			| Qt::WindowSystemMenuHint
			| Qt::WindowMinMaxButtonsHint
			| Qt::WindowCloseButtonHint;
	#ifdef QTRACTOR_MIDI_EDITOR_TOOL
		qtractorOptions *pOptions = qtractorOptions::getInstance();
		if (pOptions && pOptions->bKeepToolsOnTop)
			wflags |= Qt::Tool;
	#endif
		// Do it...
		m_pMidiEditorForm = new qtractorMidiEditorForm(pParent, wflags);
		// Set its most standing properties...
		m_pMidiEditorForm->show();
		m_pMidiEditorForm->setup(this);
	} else {
		// Just show up the editor form...
		m_pMidiEditorForm->setup();
		m_pMidiEditorForm->show();
	}

	// Get it up any way...
	m_pMidiEditorForm->raise();
	m_pMidiEditorForm->activateWindow();

	return true;
}


// Clip editor reset.
void qtractorMidiClip::resetEditor ( bool bSelectClear )
{
	if (m_pMidiEditorForm) {
		qtractorMidiEditor *pMidiEditor = m_pMidiEditorForm->editor();
		if (pMidiEditor)
			pMidiEditor->reset(bSelectClear);
		m_pMidiEditorForm->resetDirtyCount();
	}
}


// Clip editor update.
void qtractorMidiClip::updateEditor ( bool bSelectClear )
{
	if (m_pMidiEditorForm == NULL)
		return;

	qtractorMidiEditor *pMidiEditor = m_pMidiEditorForm->editor();
	if (pMidiEditor) {
		pMidiEditor->reset(bSelectClear);
		pMidiEditor->setOffset(clipStart());
		pMidiEditor->setLength(clipLength());
		qtractorTrack *pTrack = track();
		if (pTrack) {
			pMidiEditor->setForeground(pTrack->foreground());
			pMidiEditor->setBackground(pTrack->background());
		}
		pMidiEditor->updateContents();
	}

	m_pMidiEditorForm->resetDirtyCount();
	m_pMidiEditorForm->updateInstrumentNames();
	m_pMidiEditorForm->stabilizeForm();
}


// Clip query-close method (return true if editing is done).
bool qtractorMidiClip::queryEditor (void)
{
	if (m_pMidiEditorForm)
		return m_pMidiEditorForm->queryClose();
	else
		return qtractorClip::queryEditor();
}


// MIDI clip tool-tip.
QString qtractorMidiClip::toolTip (void) const
{
	QString sToolTip = qtractorClip::toolTip() + ' ';

	sToolTip += QObject::tr("(format %1)\nMIDI:\t").arg(m_iFormat);
	if (m_iFormat == 0)
		sToolTip += QObject::tr("Channel %1").arg(m_iTrackChannel + 1);
	else
		sToolTip += QObject::tr("Track %1").arg(m_iTrackChannel);

	if (m_pFile) {
		sToolTip += QObject::tr(", %1 tracks, %2 tpqn")
			.arg(m_pFile->tracks())
			.arg(m_pFile->ticksPerBeat());
	}

	if (clipGain() > 1.0f)
		sToolTip += QObject::tr(" (%1% vol)")
			.arg(100.0f * clipGain(), 0, 'g', 3);

	return sToolTip;
}


// Virtual document element methods.
bool qtractorMidiClip::loadClipElement (
	qtractorDocument * /* pDocument */, QDomElement *pElement )
{
	// Load track children...
	for (QDomNode nChild = pElement->firstChild();
			!nChild.isNull();
				nChild = nChild.nextSibling()) {
		// Convert node to element...
		QDomElement eChild = nChild.toElement();
		if (eChild.isNull())
			continue;
		// Load track state..
		if (eChild.tagName() == "filename")
			qtractorMidiClip::setFilename(eChild.text());
		else if (eChild.tagName() == "track-channel")
			qtractorMidiClip::setTrackChannel(eChild.text().toUShort());
		else if (eChild.tagName() == "revision")
			qtractorMidiClip::setRevision(eChild.text().toUShort());
	}

	return true;
}


bool qtractorMidiClip::saveClipElement (
	qtractorDocument *pDocument, QDomElement *pElement ) const
{
	QDomElement eMidiClip = pDocument->document()->createElement("midi-clip");
	pDocument->saveTextElement("filename",
		qtractorMidiClip::relativeFilename(pDocument), &eMidiClip);
	pDocument->saveTextElement("track-channel",
		QString::number(qtractorMidiClip::trackChannel()), &eMidiClip);
	pDocument->saveTextElement("revision",
		QString::number(qtractorMidiClip::revision()), &eMidiClip);
	pElement->appendChild(eMidiClip);

	return true;
}


// MIDI clip export method.
bool qtractorMidiClip::clipExport ( ClipExport pfnClipExport, void *pvArg,
	unsigned long iOffset, unsigned long iLength ) const
{
	qtractorTrack *pTrack = track();
	if (pTrack == NULL)
		return false;

	qtractorSession *pSession = pTrack->session();
	if (pSession == NULL)
		return false;

	qtractorMidiSequence *pSeq = sequence();
	if (pSeq == NULL)
		return false;

	if (iLength < 1)
		iLength = clipLength();

	unsigned short iTicksPerBeat = pSession->ticksPerBeat();
	unsigned long iTimeOffset = pSeq->timeOffset();

	qtractorTimeScale::Cursor cursor(pSession->timeScale());
	qtractorTimeScale::Node *pNode = cursor.seekFrame(clipStart());
	unsigned long t0 = pNode->tickFromFrame(clipStart());

	unsigned long f1 = clipStart() + clipOffset() + iOffset;
	pNode = cursor.seekFrame(f1);
	unsigned long t1 = pNode->tickFromFrame(f1);
	unsigned long iTimeStart = t1 - t0;
	iTimeStart = (iTimeStart > iTimeOffset ? iTimeStart - iTimeOffset : 0);
	pNode = cursor.seekFrame(f1 += iLength);
	unsigned long iTimeEnd = iTimeStart + pNode->tickFromFrame(f1) - t1;

	qtractorMidiSequence seq(pSeq->name(), pSeq->channel(), iTicksPerBeat);

	seq.setBank(pTrack->midiBank());
	seq.setProgram(pTrack->midiProgram());

	for (qtractorMidiEvent *pEvent = pSeq->events().first();
			pEvent; pEvent = pEvent->next()) {
		unsigned long iTime = pEvent->time();
		if (iTime >= iTimeStart && iTime < iTimeEnd) {
			qtractorMidiEvent *pNewEvent = new qtractorMidiEvent(*pEvent);
			pNewEvent->setTime(iTime - iTimeStart);
			if (pNewEvent->type() == qtractorMidiEvent::NOTEON) {
				pNewEvent->setVelocity((unsigned char)
					(clipGain() * float(pNewEvent->velocity())) & 0x7f);
				if (iTime + pEvent->duration() > iTimeEnd)
					pNewEvent->setDuration(iTimeEnd - iTime);
			}
			seq.insertEvent(pNewEvent);
		}
	}

	(*pfnClipExport)(&seq, pvArg);

	return true;
}


// Default MIDI file format (for capture/record) accessors.
unsigned short qtractorMidiClip::g_iDefaultFormat = 0;

void qtractorMidiClip::setDefaultFormat ( unsigned short iFormat )
{
	g_iDefaultFormat = iFormat;
}

unsigned short qtractorMidiClip::defaultFormat (void)
{
	return g_iDefaultFormat;
}


// end of qtractorMidiClip.cpp
