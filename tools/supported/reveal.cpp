// This is a reveal program. It reveals the map.

#include <iostream>
#include <vector>
#include <map>
using namespace std;

#include <DFHack.h>
#include <dfhack/extra/MapExtras.h>
#include <xgetopt.h>
#include <dfhack/modules/Gui.h>

typedef std::vector<DFHack::t_feature*> FeatureListPointer;
typedef std::map<DFHack::DFCoord, FeatureListPointer> FeatureMap;

#ifdef LINUX_BUILD
#include <unistd.h>
void waitmsec (int delay)
{
    usleep(delay);
}
#else
#include <windows.h>
void waitmsec (int delay)
{
    Sleep(delay);
}
#endif
#include <dfhack/extra/termutil.h>

/*
 * Anything that might reveal Hell is unsafe.
 */
bool isSafe(uint32_t x, uint32_t y, uint32_t z, DFHack::Maps *Maps,
            MapExtras::MapCache &cache, FeatureMap localFeatures)
{
    DFHack::t_feature *blockFeatureLocal = NULL;

    DFHack::DFCoord blockCoord(x, y);
    MapExtras::Block *b = cache.BlockAt(DFHack::DFCoord(x, y, z));

    uint16_t index = b->raw.local_feature;
    FeatureMap::const_iterator it = localFeatures.find(blockCoord);
    if (it != localFeatures.end())
    {
        FeatureListPointer features = it->second;

        if (index != -1 && index < features.size())
        {
            blockFeatureLocal = features[index];
        }
    }

    if (blockFeatureLocal == NULL)
        return true;

    // Adamantine tubes and temples lead to Hell, and Hell *is* Hell.
    if (blockFeatureLocal->type != DFHack::feature_Other)
        return false;

    return true;
}

struct hideblock
{
    uint32_t x;
    uint32_t y;
    uint32_t z;
    uint8_t hiddens [16][16];
};

int main (int argc, char *argv[])
{
    bool doSafe = false;

    char c;
    xgetopt opt(argc, argv, "s");
    opt.opterr = 0;

    while ((c = opt()) != -1)
    {
        switch (c)
        {
        case 's':
            doSafe = true;
            break;
        case '?':
            switch (opt.optopt)
            {
            // For when we take arguments
            default:
                if (isprint(opt.optopt))
                    std::cerr << "Unknown option -" << opt.optopt << "!"
                            << std::endl;
                else
                    std::cerr << "Unknown option character " << (int) opt.optopt << "!"
                            << std::endl;
            }
        default:
            // Um.....
            return 1;
        }
    }

    bool temporary_terminal = TemporaryTerminal();
    uint32_t x_max,y_max,z_max;
    DFHack::designations40d designations;
    DFHack::ContextManager DFMgr("Memory.xml");
    DFHack::Context *DF;
    try
    {
        DF = DFMgr.getSingleContext();
        DF->Attach();
    }
    catch (exception& e)
    {
        cerr << e.what() << endl;
        if(temporary_terminal)
            cin.ignore();
        return 1;
    }
    DFHack::Maps *Maps =DF->getMaps();
    DFHack::World *World =DF->getWorld();

    // walk the map, save the hide bits, reveal.
    cout << "Pausing..." << endl;

    // horrible hack to make sure the pause is really set
    // preblem here is that we could be 'arriving' at the wrong time and DF could be in the middle of a frame.
    // that could mean that revealing, even with suspending DF's thread, would mean unleashing hell *in the same frame* 
    // this here hack sets the pause state, resumes DF, waits a second for it to enter the pause (I know, BS value.) and suspends.
    World->SetPauseState(true);
    DF->Resume();
    waitmsec(1000);
    DF->Suspend();

    cout << "Revealing, please wait..." << endl;

    // init the map
    if(!Maps->Start())
    {
        cerr << "Can't init map." << endl;
        if(temporary_terminal)
            cin.ignore();
        return 1;
    }

    MapExtras::MapCache cache(Maps);
    FeatureMap localFeatures;

    if(doSafe && !Maps->ReadLocalFeatures(localFeatures))
    {
        std::cerr << "Unable to read local features; can't reveal map "
                  << "safely" << std::endl;
        if(temporary_terminal)
            cin.ignore();
        return 1;
    }

    Maps->getSize(x_max,y_max,z_max);
    vector <hideblock> hidesaved;

    // We go from the top z-level down, stopping as soon as we encounter
    // something that might lead to Hell, so the player can unpause without
    // spawning demons.
    bool quit = false;
    for(uint32_t z = z_max - 1; z > 0 && !quit;z--)
    {
        for(uint32_t y = 0; y < y_max;y++)
        {
            for(uint32_t x = 0; x < x_max;x++)
            {
                if(Maps->isValidBlock(x,y,z))
                {
                    if (doSafe && !isSafe(x, y, z, Maps, cache, localFeatures))
                        quit = true;

                    hideblock hb;
                    hb.x = x;
                    hb.y = y;
                    hb.z = z;
                    // read block designations
                    Maps->ReadDesignations(x,y,z, &designations);
                    // change the hidden flag to 0
                    for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
                    {
                        hb.hiddens[i][j] = designations[i][j].bits.hidden;
                        designations[i][j].bits.hidden = 0;
                    }
                    hidesaved.push_back(hb);
                    // write the designations back
                    Maps->WriteDesignations(x,y,z, &designations);
                }
            }
        }
    }
    // FIXME: force game pause here!
    DF->Detach();
    cout << "Map revealed. The game has been paused for you." << endl;
    if (doSafe)
        cout << "Unpausing *WON'T* reveal hell." << endl << endl;
    else
        cout << "Unpausing can unleash the forces of hell!" << endl << endl;
    cout << "Press any key to unreveal." << endl;
    cout << "Close to keep the map revealed !!FOREVER!!" << endl;
    cin.ignore();
    cout << "Unrevealing... please wait." << endl;
    // FIXME: do some consistency checks here!
    DF->Attach();
    Maps = DF->getMaps();
    Maps->Start();
    for(size_t i = 0; i < hidesaved.size();i++)
    {
        hideblock & hb = hidesaved[i];
        Maps->ReadDesignations(hb.x,hb.y,hb.z, &designations);
        for (uint32_t i = 0; i < 16;i++) for (uint32_t j = 0; j < 16;j++)
        {
            designations[i][j].bits.hidden = hb.hiddens[i][j];
        }
        Maps->WriteDesignations(hb.x,hb.y,hb.z, &designations);
    }
    if(temporary_terminal)
    {
        cout << "Done. Press any key to continue" << endl;
        cin.ignore();
    }
    return 0;
}
