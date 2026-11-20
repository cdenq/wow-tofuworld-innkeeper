// wow-tofuworld-innkeeper: script loader. Deployed as mod-tofu-innkeeper.
// The core calls Add<folder name with '-' replaced by '_'>Scripts() for each module folder in C:\azerothcore\modules.

void AddSC_tofu_innkeeper();

void Addmod_tofu_innkeeperScripts()
{
    AddSC_tofu_innkeeper();
}
