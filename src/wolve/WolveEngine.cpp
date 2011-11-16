//----------------------------------------------------------------------------
/** @file WolveEngine.cpp */
//----------------------------------------------------------------------------

#include "SgSystem.h"

#include "BitsetIterator.hpp"
#include "Misc.hpp"
#include "PlayAndSolve.hpp"
#include "SwapCheck.hpp"
#include "WolveEngine.hpp"
#include "WolvePlayer.hpp"
#include "WolveTimeControl.hpp"

using namespace benzene;

//----------------------------------------------------------------------------

namespace {

//----------------------------------------------------------------------------

template<typename TYPE>
void ParseDashSeparatedString(const std::string& str, std::vector<TYPE>& out)
{
    // remove the '-' separators
    std::string widths(str);
    for (std::size_t i = 0; i < widths.size(); ++i)
        if (widths[i] == '-') widths[i] = ' ';

    // parse the ' ' separated widths
    std::istringstream is;
    is.str(widths);
    while (is)
    {
        TYPE j;
        if (is >> j)
            out.push_back(j);
    }
}

//----------------------------------------------------------------------------

} // namespace

//----------------------------------------------------------------------------

WolveEngine::WolveEngine(int boardsize, WolvePlayer& player)
    : CommonHtpEngine(boardsize),
      m_player(player)
{
    RegisterCmd("param_wolve", &WolveEngine::CmdParam);
    RegisterCmd("wolve-get-pv", &WolveEngine::CmdGetPV);
    RegisterCmd("wolve-scores", &WolveEngine::CmdScores);
    RegisterCmd("wolve-data", &WolveEngine::CmdData);
    RegisterCmd("wolve-clear-hash", &WolveEngine::CmdClearHash);
}

WolveEngine::~WolveEngine()
{
}

//----------------------------------------------------------------------------

void WolveEngine::RegisterCmd(const std::string& name,
                              GtpCallback<WolveEngine>::Method method)
{
    Register(name, new GtpCallback<WolveEngine>(this, method));
}

//----------------------------------------------------------------------------

double WolveEngine::TimeForMove(HexColor c)
{
    if (m_player.UseTimeManagement())
        return WolveTimeControl::TimeForMove(m_game, m_game.TimeRemaining(c));
    return m_player.MaxTime();
}

/** Generates a move. */
HexPoint WolveEngine::GenMove(HexColor color, bool useGameClock)
{
    SG_UNUSED(useGameClock);
    if (SwapCheck::PlaySwap(m_game, color))
        return SWAP_PIECES;
    HexState state(m_game.Board(), color);
    double maxTime = TimeForMove(color);
    if (m_useParallelSolver)
    {
        PlayAndSolve ps(*m_pe.brd, *m_se.brd, m_player, m_dfpnSolver, 
                        m_dfpnPositions, m_game);
        return ps.GenMove(state, maxTime);
    }
    double score;
    return m_player.GenMove(state, m_game, m_pe.SyncBoard(m_game.Board()),
                            maxTime, score);
}

void WolveEngine::CmdAnalyzeCommands(HtpCommand& cmd)
{
    CommonHtpEngine::CmdAnalyzeCommands(cmd);
    cmd <<
        "param/Wolve Param/param_wolve\n"
        "var/Wolve PV/wolve-get-pv\n"
        "pspairs/Wolve Scores/wolve-scores\n"
        "none/Wolve Clear Hashtable/wolve-clear-hash\n"
        "scores/Wolve Data/wolve-data\n";
}

/** Wolve parameters. */
void WolveEngine::CmdParam(HtpCommand& cmd)
{
    WolveSearch& search = m_player.Search();

    if (cmd.NuArg() == 0) 
    {
        cmd << '\n'
            << "[bool] backup_ice_info "
            << search.BackupIceInfo() << '\n'
            << "[bool] use_guifx "
            << search.GuiFx() << '\n'
            << "[bool] search_singleton "
            << m_player.SearchSingleton() << '\n'
            << "[bool] use_parallel_solver "
            << m_useParallelSolver << '\n'
            << "[bool] use_time_management "
            << m_player.UseTimeManagement() << '\n'
            << "[string] ply_width " 
            << MiscUtil::PrintVector(search.PlyWidth()) << '\n'
            << "[string] max_depth "
            << m_player.MaxDepth() << '\n'
            << "[string] max_time "
            << m_player.MaxTime() << '\n'
            << "[string] min_depth "
            << m_player.MinDepth() << '\n'
            << "[string] tt_bits "
            << (m_player.HashTable() 
                ? log2(m_player.HashTable()->MaxHash()) : 0);
    }
    else if (cmd.NuArg() == 2)
    {
        std::string name = cmd.Arg(0);
        if (name == "backup_ice_info")
            search.SetBackupIceInfo(cmd.Arg<bool>(1));
        else if (name == "max_time")
            m_player.SetMaxTime(cmd.Arg<float>(1));
        else if (name == "ply_width")
        {
            std::vector<std::size_t> plywidth;
            ParseDashSeparatedString(cmd.Arg(1), plywidth);
            search.SetPlyWidth(plywidth);
        } 
        else if (name == "max_depth")
            m_player.SetMaxDepth(cmd.ArgMin<std::size_t>(1, 1));
        else if (name == "min_depth")
            m_player.SetMinDepth(cmd.ArgMin<std::size_t>(1, 1));
        else if (name == "use_guifx")
            search.SetGuiFx(cmd.Arg<bool>(1));
        else if (name == "search_singleton")
            m_player.SetSearchSingleton(cmd.Arg<bool>(1));
        else if (name == "tt_bits")
	{
	    int bits = cmd.ArgMin<int>(1, 0);
	    if (bits == 0)
		m_player.SetHashTable(0);
	    else
		m_player.SetHashTable(new SgSearchHashTable(1 << bits));
        }
        else if (name == "use_parallel_solver")
            m_useParallelSolver = cmd.Arg<bool>(1);
        else if (name == "use_time_management")
            m_player.SetUseTimeManagement(cmd.Arg<bool>(1));
        else
            throw HtpFailure() << "Unknown parameter: " << name;
    }
    else
        throw HtpFailure("Expected 0 or 2 arguments");
}

void WolveEngine::CmdGetPV(HtpCommand& cmd)
{
    HexState state(m_game.Board(), m_game.Board().WhoseTurn());
    const SgSearchHashTable* hashTable = m_player.HashTable();
    std::vector<HexPoint> seq;
    WolveSearchUtil::ExtractPVFromHashTable(state, *hashTable, seq);
    for (std::size_t i = 0; i < seq.size(); ++i)
        cmd << seq[i] << ' ';
}

/** Prints scores of moves. */
void WolveEngine::CmdScores(HtpCommand& cmd)
{
    HexState state(m_game.Board(), m_game.Board().WhoseTurn());
    const SgSearchHashTable* hashTable = m_player.HashTable();
    if (!hashTable)
        throw HtpFailure("No hashtable!");
    cmd << WolveSearchUtil::PrintScores(state, *hashTable);
}

/** Returns data on this state in the hashtable. */
void WolveEngine::CmdData(HtpCommand& cmd)
{
    const SgSearchHashTable* hashTable = m_player.HashTable();
    if (!hashTable)
        throw HtpFailure("No hashtable!");
    SgSearchHashData data;
    HexState state(m_game.Board(), m_game.Board().WhoseTurn());
    if (hashTable->Lookup(state.Hash(), &data))
        cmd << "[score=" << data.Value()
            << " bestMove=" << m_player.Search().MoveString(data.BestMove())
            << " isExact=" << data.IsExactValue()
            << " isLower=" << data.IsOnlyLowerBound()
            << " isUpper=" << data.IsOnlyUpperBound()
            << " depth=" << data.Depth()
            << ']';
}

void WolveEngine::CmdClearHash(HtpCommand& cmd)
{
    cmd.CheckArgNone();
    SgSearchHashTable* hashTable = m_player.HashTable();
    if (!hashTable)
        throw HtpFailure("No hashtable!");
    hashTable->Clear();
}

//----------------------------------------------------------------------------
// Pondering

#if GTPENGINE_PONDER

void WolveEngine::InitPonder()
{
}

void WolveEngine::Ponder()
{
}

void WolveEngine::StopPonder()
{
}

#endif // GTPENGINE_PONDER

//----------------------------------------------------------------------------
