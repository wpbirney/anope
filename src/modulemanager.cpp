/* Modular support
 *
 * (C) 2009 Anope Team
 * Contact us at team@anope.org
 *
 * Please read COPYING and README for further details.
 */

#include "modules.h"
#include "language.h"
#include "version.h"
#include <algorithm> // std::find

std::vector<Module *> ModuleManager::EventHandlers[I_END];

void ModuleManager::LoadModuleList(std::list<std::string> &ModuleList)
{
	for (std::list<std::string>::iterator it = ModuleList.begin(), it_end = ModuleList.end(); it != it_end; ++it)
	{
		Module *m = FindModule(*it);
		if (!m)
			ModuleManager::LoadModule(*it, NULL);
	}
}

void ModuleManager::LoadModuleList(std::list<ci::string> &ModuleList)
{
	for (std::list<ci::string>::iterator it = ModuleList.begin(), it_end = ModuleList.end(); it != it_end; ++it)
	{
		Module *m = FindModule(*it);
		if (!m)
			ModuleManager::LoadModule(*it, NULL);
	}
}

/**
 * Copy the module from the modules folder to the runtime folder.
 * This will prevent module updates while the modules is loaded from
 * triggering a segfault, as the actaul file in use will be in the
 * runtime folder.
 * @param name the name of the module to copy
 * @param output the destination to copy the module to
 * @return MOD_ERR_OK on success
 */
static int moduleCopyFile(const char *name, const char *output)
{
	int ch;
	FILE *source, *target;
#ifndef _WIN32
	int srcfp;
#endif
	char input[4096];

	strlcpy(input, services_dir.c_str(), sizeof(input));
	strlcat(input, "/modules/", sizeof(input));  /* Get full path with module extension */
	strlcat(input, name, sizeof(input));
	strlcat(input, MODULE_EXT, sizeof(input));

	if (!(source = fopen(input, "rb")))
		return MOD_ERR_NOEXIST;

#ifndef _WIN32
	if ((srcfp = mkstemp(const_cast<char *>(output))) == -1)
		return MOD_ERR_FILE_IO;
#else
	if (!mktemp(const_cast<char *>(output)))
		return MOD_ERR_FILE_IO;
#endif

	Alog(LOG_DEBUG) << "Runtime module location: " << output;

#ifndef _WIN32
	if (!(target = fdopen(srcfp, "w")))
#else
	if (!(target = fopen(output, "wb")))
#endif
		return MOD_ERR_FILE_IO;
	while ((ch = fgetc(source)) != EOF)
		fputc(ch, target);
	fclose(source);
	if (fclose(target))
		return MOD_ERR_FILE_IO;
	return MOD_ERR_OK;
}

static bool IsOneOfModuleTypeLoaded(MODType mt)
{
	int pmods = 0;

	for (std::deque<Module *>::iterator it = Modules.begin(), it_end = Modules.end(); it != it_end; ++it)
	{
		if ((*it)->type == mt)
			++pmods;
	}

	/*
	 * 2, because module constructors now add modules to the hash.. so 1 (original module)
	 * and 2 (this module). -- w00t
	 */
	if (pmods == 2)
		return true;

	return false;
}

/* This code was found online at http://www.linuxjournal.com/article/3687#comment-26593
 *
 * This function will take a pointer from either dlsym or GetProcAddress and cast it in
 * a way that won't cause C++ warnings/errors to come up.
 */
template <class TYPE> TYPE function_cast(ano_module_t symbol)
{
	union
	{
		ano_module_t symbol;
		TYPE function;
	} cast;
	cast.symbol = symbol;
	return cast.function;
}

int ModuleManager::LoadModule(const std::string &modname, User *u)
{
	const char *err;
	Module *(*func)(const std::string &, const std::string &);
	int ret = 0;

	if (modname.empty())
		return MOD_ERR_PARAMS;

	if (FindModule(modname) != NULL)
		return MOD_ERR_EXISTS;

	Alog(LOG_DEBUG) << "trying to load [" << modname <<  "]";

	/* Generate the filename for the temporary copy of the module */
	std::string pbuf;
	pbuf = services_dir + "/modules/";
#ifndef _WIN32
	pbuf += "runtime/";
#else
	pbuf += "runtime\\";
#endif
	pbuf += modname;
	pbuf += MODULE_EXT;
	pbuf += ".";
	pbuf += "XXXXXX";

	/* Don't skip return value checking! -GD */
	if ((ret = moduleCopyFile(modname.c_str(), pbuf.c_str())) != MOD_ERR_OK)
	{
		/* XXX: This used to assign filename here, but I don't think that was correct..
		 * even if it was, it makes life very fucking difficult, so.
		 */
		return ret;
	}

	ano_modclearerr();

	ano_module_t handle = dlopen(pbuf.c_str(), RTLD_LAZY);
	if (!handle && (err = dlerror()))
	{
		Alog() << err;
		return MOD_ERR_NOLOAD;
	}

	ano_modclearerr();
	func = function_cast<Module *(*)(const std::string &, const std::string &)>(dlsym(handle, "AnopeInit"));
	if (!func && (err = dlerror()))
	{
		Alog() << "No init function found, not an Anope module";
		dlclose(handle);
		return MOD_ERR_NOLOAD;
	}

	if (!func)
		throw CoreException("Couldn't find constructor, yet moderror wasn't set?");

	/* Create module. */
	std::string nick;
	if (u)
		nick = u->nick;
	else
		nick = "";

	Module *m;

	try
	{
		m = func(modname, nick);
	}
	catch (const ModuleException &ex)
	{
		Alog() << "Error while loading " << modname << ": " << ex.GetReason();
		return MOD_STOP;
	}

	m->filename = pbuf;
	m->handle = handle;

	Version v = m->GetVersion();
	if (v.GetMajor() < VERSION_MAJOR || (v.GetMajor() == VERSION_MAJOR && v.GetMinor() < VERSION_MINOR))
	{
		Alog() << "Module " << modname << " is compiled against an older version of Anope " << v.GetMajor() << "." << v.GetMinor() << ", this is " << VERSION_MAJOR << "." << VERSION_MINOR;
		DeleteModule(m);
		return MOD_STOP;
	}
	else if (v.GetMajor() > VERSION_MAJOR || (v.GetMajor() == VERSION_MAJOR && v.GetMinor() > VERSION_MINOR))
	{
		Alog() << "Module " << modname << " is compiled against a newer version of Anope " << v.GetMajor() << "." << v.GetMinor() << ", this is " << VERSION_MAJOR << "." << VERSION_MINOR;
		DeleteModule(m);
		return MOD_STOP;
	}
	else if (v.GetBuild() < VERSION_BUILD)
		Alog() << "Module " << modname << " is compiled against an older revision of Anope " << v.GetBuild() << ", this is " << VERSION_BUILD;
	else if (v.GetBuild() > VERSION_BUILD)
		Alog() << "Module " << modname << " is compiled against a newer revision of Anope " << v.GetBuild() << ", this is " << VERSION_BUILD;
	else if (v.GetBuild() == VERSION_BUILD)
		Alog(LOG_DEBUG) << "Module " << modname << " compiled against current version of Anope " << v.GetBuild();

	if (m->type == PROTOCOL && IsOneOfModuleTypeLoaded(PROTOCOL))
	{
		DeleteModule(m);
		Alog() << "You cannot load two protocol modules";
		return MOD_STOP;
	}

	if (u)
	{
		ircdproto->SendGlobops(OperServ, "%s loaded module %s", u->nick.c_str(), modname.c_str());
		notice_lang(Config.s_OperServ, u, OPER_MODULE_LOADED, modname.c_str());

		/* If a user is loading this module, then the core databases have already been loaded
		 * so trigger the event manually
		 */
		m->OnPostLoadDatabases();
	}

	FOREACH_MOD(I_OnModuleLoad, OnModuleLoad(u, m));

	return MOD_ERR_OK;
}

int ModuleManager::LoadModule(const char *modname, User *u)
{
	return LoadModule(std::string(modname), u);
}

int ModuleManager::LoadModule(const ci::string &modname, User *u)
{
	return LoadModule(std::string(modname.c_str()), u);
}

int ModuleManager::UnloadModule(Module *m, User *u)
{
	if (!m || !m->handle)
	{
		if (u)
			notice_lang(Config.s_OperServ, u, OPER_MODULE_REMOVE_FAIL, m->name.c_str());
		return MOD_ERR_PARAMS;
	}

	if (m->GetPermanent() || m->type == PROTOCOL || m->type == ENCRYPTION || m->type == DATABASE)
	{
		if (u)
			notice_lang(Config.s_OperServ, u, OPER_MODULE_NO_UNLOAD);
		return MOD_ERR_NOUNLOAD;
	}

	if (u)
	{
		ircdproto->SendGlobops(OperServ, "%s unloaded module %s", u->nick.c_str(), m->name.c_str());
		notice_lang(Config.s_OperServ, u, OPER_MODULE_UNLOADED, m->name.c_str());
	}

	FOREACH_MOD(I_OnModuleUnload, OnModuleUnload(u, m));

	DeleteModule(m);
	return MOD_ERR_OK;
}

void ModuleManager::DeleteModule(Module *m)
{
	const char *err;
	void (*destroy_func)(Module *m);
	ano_module_t handle;

	if (!m || !m->handle) /* check m is least possibly valid */
		return;

	DetachAll(m);
	handle = m->handle;

	ano_modclearerr();
	destroy_func = function_cast<void (*)(Module *)>(dlsym(m->handle, "AnopeFini"));
	if (!destroy_func && (err = dlerror()))
	{
		Alog() << "No destroy function found, chancing delete...";
		delete m; /* we just have to chance they haven't overwrote the delete operator then... */
	}
	else
		destroy_func(m); /* Let the module delete it self, just in case */

	if (handle)
	{
		if (dlclose(handle))
			Alog() << dlerror();
	}
}

bool ModuleManager::Attach(Implementation i, Module *mod)
{
	if (std::find(EventHandlers[i].begin(), EventHandlers[i].end(), mod) != EventHandlers[i].end())
		return false;

	EventHandlers[i].push_back(mod);
	return true;
}

bool ModuleManager::Detach(Implementation i, Module *mod)
{
	std::vector<Module *>::iterator x = std::find(EventHandlers[i].begin(), EventHandlers[i].end(), mod);

	if (x == EventHandlers[i].end())
		return false;

	EventHandlers[i].erase(x);
	return true;
}

void ModuleManager::Attach(Implementation *i, Module *mod, size_t sz)
{
	for (size_t n = 0; n < sz; ++n)
		Attach(i[n], mod);
}

void ModuleManager::DetachAll(Module *mod)
{
	for (size_t n = I_BEGIN + 1; n != I_END; ++n)
		Detach(static_cast<Implementation>(n), mod);
}

bool ModuleManager::SetPriority(Module *mod, Priority s)
{
	for (size_t n = I_BEGIN + 1; n != I_END; ++n)
		SetPriority(mod, static_cast<Implementation>(n), s);

	return true;
}

bool ModuleManager::SetPriority(Module *mod, Implementation i, Priority s, Module **modules, size_t sz)
{
	/** To change the priority of a module, we first find its position in the vector,
	 * then we find the position of the other modules in the vector that this module
	 * wants to be before/after. We pick off either the first or last of these depending
	 * on which they want, and we make sure our module is *at least* before or after
	 * the first or last of this subset, depending again on the type of priority.
	 */
	size_t swap_pos = 0;
	size_t source = 0;
	bool swap = true;
	bool found = false;

	/* Locate our module. This is O(n) but it only occurs on module load so we're
	 * not too bothered about it
	 */
	for (size_t x = 0, end = EventHandlers[i].size(); x != end; ++x)
	{
		if (EventHandlers[i][x] == mod)
		{
			source = x;
			found = true;
			break;
		}
	}

	/* Eh? this module doesnt exist, probably trying to set priority on an event
	 * theyre not attached to.
	 */
	if (!found)
		return false;

	switch (s)
	{
		/* Dummy value */
		case PRIORITY_DONTCARE:
			swap = false;
			break;
		/* Module wants to be first, sod everything else */
		case PRIORITY_FIRST:
			swap_pos = 0;
			break;
		/* Module is submissive and wants to be last... awww. */
		case PRIORITY_LAST:
			if (EventHandlers[i].empty())
				swap_pos = 0;
			else
				swap_pos = EventHandlers[i].size() - 1;
			break;
		/* Place this module after a set of other modules */
		case PRIORITY_AFTER:
			{
				/* Find the latest possible position */
				swap_pos = 0;
				swap = false;
				for (size_t x = 0, end = EventHandlers[i].size(); x != end; ++x)
				{
					for (size_t n = 0; n < sz; ++n)
					{
						if (modules[n] && EventHandlers[i][x] == modules[n] && x >= swap_pos && source <= swap_pos)
						{
							swap_pos = x;
							swap = true;
						}
					}
				}
			}
			break;
		/* Place this module before a set of other modules */
		case PRIORITY_BEFORE:
			{
				swap_pos = EventHandlers[i].size() - 1;
				swap = false;
				for (size_t x = 0, end = EventHandlers[i].size(); x != end; ++x)
				{
					for (size_t n = 0; n < sz; ++n)
					{
						if (modules[n] && EventHandlers[i][x] == modules[n] && x <= swap_pos && source >= swap_pos)
						{
							swap = true;
							swap_pos = x;
						}
					}
				}
			}
	}

	/* Do we need to swap? */
	if (swap && swap_pos != source)
	{
		/* Suggestion from Phoenix, "shuffle" the modules to better retain call order */
		int incrmnt = 1;

		if (source > swap_pos)
			incrmnt = -1;

		for (unsigned int j = source; j != swap_pos; j += incrmnt)
		{
			if (j + incrmnt > EventHandlers[i].size() - 1 || j + incrmnt < 0)
				continue;

			std::swap(EventHandlers[i][j], EventHandlers[i][j + incrmnt]);
		}
	}

	return true;
}

/** Delete all callbacks attached to a module
 * @param m The module
 */
void ModuleManager::ClearCallBacks(Module *m)
{
	while (!m->CallBacks.empty())
		delete m->CallBacks.front();
}

/** Unloading all modules, NEVER call this when Anope isn't shutting down.
 * Ever.
 * @param unload_proto true to unload the protocol module
 */
void ModuleManager::UnloadAll(bool unload_proto)
{
	for (std::deque<Module *>::iterator it = Modules.begin(), it_end = Modules.end(); it != it_end; )
	{
		Module *m = *it++;

		if (unload_proto || m->type != PROTOCOL)
			DeleteModule(m);

		if (Modules.empty())
			break;
	}
}
