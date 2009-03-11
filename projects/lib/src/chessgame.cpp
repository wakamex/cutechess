/*
    This file is part of Cute Chess.

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Cute Chess is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Cute Chess.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "chessgame.h"
#include <QtDebug>
#include "chessboard/chessboard.h"
#include "chessplayer.h"
#include "openingbook.h"
#include "pgngame.h"


ChessGame::ChessGame(Chess::Variant variant, QObject* parent)
	: QObject(parent),
	  PgnGame(variant),
	  m_gameInProgress(false),
	  m_inOpening(false)
{
	m_player[Chess::White] = 0;
	m_player[Chess::Black] = 0;
	m_board = new Chess::Board(variant, this);
}

Chess::Board* ChessGame::board() const
{
	return m_board;
}

ChessPlayer* ChessGame::playerToMove()
{
	if (m_board->sideToMove() == Chess::NoSide)
		return 0;
	return m_player[m_board->sideToMove()];
}

ChessPlayer* ChessGame::playerToWait()
{
	if (m_board->sideToMove() == Chess::NoSide)
		return 0;
	return m_player[!((int)m_board->sideToMove())];
}

void ChessGame::endGame()
{
	if (!m_gameInProgress)
		return;
	
	m_gameInProgress = false;
	qDebug() << "Game ended:" << m_result.toString();

	m_player[Chess::White]->endGame(m_result);
	m_player[Chess::Black]->endGame(m_result);

	connect(this, SIGNAL(playersReady()), this, SIGNAL(gameEnded()), Qt::QueuedConnection);
	syncPlayers(true);
}

void ChessGame::onMoveMade(const Chess::Move& move)
{
	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());
	Q_ASSERT(sender != 0);
	
	Q_ASSERT(m_gameInProgress);
	Q_ASSERT(m_board->isLegalMove(move));
	if (sender != playerToMove())
	{
		qDebug() << sender->name() << "tried to make a move on the opponent's turn";
		return;
	}

	// Opening moves are already in the list
	if (!m_inOpening)
		m_moves.append(move);

	playerToWait()->makeMove(move);
	m_board->makeMove(move, true);
	
	m_result = m_board->result();
	if (m_result.isNone())
		playerToMove()->go();
	else
		endGame();
	
	emit moveMade(move);
}

void ChessGame::onForfeit(Chess::Result result)
{
	if (!m_gameInProgress)
		return;

	m_result = result;
	endGame();
}

Chess::Move ChessGame::bookMove(const OpeningBook* book)
{
	if (book == 0)
		return Chess::Move(0, 0);
	
	BookMove bookMove = book->move(m_board->key());
	return m_board->moveFromBook(bookMove);
}

void ChessGame::setPlayer(Chess::Side side, ChessPlayer* player)
{
	Q_ASSERT(side != Chess::NoSide);
	Q_ASSERT(player != 0);
	m_player[side] = player;

	player->setBoard(m_board);
	connect(player, SIGNAL(moveMade(const Chess::Move&)),
	        this, SLOT(onMoveMade(const Chess::Move&)));
	connect(player, SIGNAL(forfeit(Chess::Result)),
		this, SLOT(onForfeit(Chess::Result)));
}

bool ChessGame::setFenString(const QString& fen)
{
	if (!m_board->setBoard(fen))
		return false;
	m_fen = fen;
	return true;
}

void ChessGame::setOpeningMoves(const OpeningBook* book, int maxMoves)
{
	Q_ASSERT(book != 0);
	Q_ASSERT(!m_gameInProgress);
	
	setBoard();
	m_moves.clear();
	for (int i = 0; i < maxMoves; i++)
	{
		Chess::Move move = bookMove(book);
		if (!m_board->isLegalMove(move)
		||  m_board->isRepeatMove(move))
			break;
		
		m_moves.append(move);
		m_board->makeMove(move);
	}
	m_board->setBoard(m_fen);
}

void ChessGame::setBoard()
{
	if (m_fen.isEmpty())
	{
		m_fen = m_board->variant().startingFen();
		// The default starting positions, even those generated
		// for random variants, should never fail.
		if (!m_board->setBoard(m_fen))
			qFatal("Invalid FEN: %s", qPrintable(m_fen));
	}
}

bool ChessGame::arePlayersReady() const
{
	return (m_player[0]->isReady() && m_player[1]->isReady());
}

void ChessGame::syncPlayers(bool ignoreSender)
{
	ChessPlayer* sender = qobject_cast<ChessPlayer*>(QObject::sender());

	if (ignoreSender && sender != 0)
	{
		// There should be no sender, but for some reason we've got one
		disconnect(sender, SIGNAL(ready()), this, SLOT(syncPlayers()));
		sender = 0;
	}

	if (!sender)
	{
		bool ready = true;
		for (int i = 0; i < 2; i++)
		{
			if (!m_player[i]->isReady())
			{
				ready = false;
				connect(m_player[i], SIGNAL(ready()),
					this, SLOT(syncPlayers()));
			}
		}
		if (!ready)
			return;
	}
	else
	{
		disconnect(sender, SIGNAL(ready()), this, SLOT(syncPlayers()));
		if (!arePlayersReady())
			return;
	}

	emit playersReady();
}

void ChessGame::start()
{
	if (!arePlayersReady())
	{
		connect(this, SIGNAL(playersReady()), this, SLOT(start()));
		syncPlayers(true);
		return;
	}
	disconnect(this, SIGNAL(playersReady()), this, SLOT(start()));
	
	for (int i = 0; i < 2; i++)
	{
		ChessPlayer* player = m_player[i];
		Q_ASSERT(player != 0);
		Q_ASSERT(player->isReady());
		
		if (!player->supportsVariant(m_board->variant()))
		{
			qDebug() << player->name() << "doesn't support"
			         << m_board->variant().toString() << "chess.";
			return;
		}	
	}
	
	setBoard();
	for (int i = 0; i < 2; i++)
	{
		setPlayerName((Chess::Side)i, m_player[i]->name());
		m_timeControl[i] = *m_player[i]->timeControl();
		m_player[i]->newGame((Chess::Side)i, m_player[!i]);
	}
	
	m_gameInProgress = true;
	m_hasTags = true;
	
	// Play the forced opening moves first
	m_inOpening = true;
	foreach (const Chess::Move& move, m_moves)
	{
		Q_ASSERT(m_board->isLegalMove(move));
		
		playerToMove()->makeBookMove(move);
		playerToWait()->makeMove(move);
		m_board->makeMove(move, true);
		
		emit moveMade(move);
	}
	m_inOpening = false;
	
	playerToMove()->go();
}

ChessPlayer* ChessGame::player(Chess::Side side) const
{
	Q_ASSERT(side != Chess::NoSide);
	return m_player[side];
}
