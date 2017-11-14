#include <meta/VersionList.h>
#include <meta/Index.h>
#include <Env.h>
#include "Component.h"

#include "meta/Version.h"
#include "VersionFile.h"
#include "minecraft/ComponentList.h"

Component::Component(std::shared_ptr<Meta::Version> version)
	:m_metaVersion(version)
{
	this->uid = version->uid();
}

Component::Component(const QString& uid, std::shared_ptr<VersionFile> file, const QString& filename)
	:m_file(file), m_filename(filename)
{
	this->uid = uid;
}

std::shared_ptr<Meta::Version> Component::getMeta()
{
	return m_metaVersion;
}

void Component::applyTo(LaunchProfile* profile)
{
	auto vfile = getVersionFile();
	if(vfile)
	{
		vfile->applyTo(profile);
	}
	else
	{
		profile->applyProblemSeverity(getProblemSeverity());
	}
}

std::shared_ptr<class VersionFile> Component::getVersionFile() const
{
	if(m_metaVersion)
	{
		if(!m_metaVersion->isLoaded())
		{
			m_metaVersion->load();
		}
		return m_metaVersion->data();
	}
	else
	{
		return m_file;
	}
}

std::shared_ptr<class Meta::VersionList> Component::getVersionList() const
{
	// FIXME: what if the metadata index isn't loaded yet?
	if(ENV.metadataIndex()->hasUid(uid))
	{
		return ENV.metadataIndex()->get(uid);
	}
	return nullptr;
}

int Component::getOrder()
{
	if(m_orderOverride)
		return m_order;

	auto vfile = getVersionFile();
	if(vfile)
	{
		return vfile->order;
	}
	return 0;
}
void Component::setOrder(int order)
{
	m_orderOverride = true;
	m_order = order;
}
QString Component::getID()
{
	return uid;
}
QString Component::getName()
{
	if (!cachedName.isEmpty())
		return cachedName;
	return uid;
}
QString Component::getVersion()
{
	if(m_metaVersion)
		return m_metaVersion->version();
	auto vfile = getVersionFile();
	if(vfile)
	{
		return vfile->version;
	}
	return currentVersion;
}
QString Component::getFilename()
{
	return m_filename;
}
QDateTime Component::getReleaseDateTime()
{
	if(m_metaVersion)
	{
		return m_metaVersion->time();
	}
	auto vfile = getVersionFile();
	if(vfile)
	{
		return vfile->releaseTime;
	}
	// FIXME: fake
	return QDateTime::currentDateTime();
}

bool Component::isCustom()
{
	return !m_isVanilla;
};

bool Component::isCustomizable()
{
	if(m_metaVersion)
	{
		if(getVersionFile())
		{
			return true;
		}
	}
	return false;
}
bool Component::isRemovable()
{
	return m_isRemovable;
}
bool Component::isRevertible()
{
	return m_isRevertible;
}
bool Component::isMoveable()
{
	return m_isMovable;
}
bool Component::isVersionChangeable()
{
	auto list = getVersionList();
	if(list)
	{
		if(!list->isLoaded())
		{
			list->load();
		}
		return list->count() != 0;
	}
	return false;
}

void Component::setVanilla (bool state)
{
	m_isVanilla = state;
}
void Component::setRemovable (bool state)
{
	m_isRemovable = state;
}
void Component::setRevertible (bool state)
{
	m_isRevertible = state;
}
void Component::setMovable (bool state)
{
	m_isMovable = state;
}

ProblemSeverity Component::getProblemSeverity() const
{
	auto file = getVersionFile();
	if(file)
	{
		return file->getProblemSeverity();
	}
	return ProblemSeverity::Error;
}

const QList<PatchProblem> Component::getProblems() const
{
	auto file = getVersionFile();
	if(file)
	{
		return file->getProblems();
	}
	return {{ProblemSeverity::Error, QObject::tr("Patch is not loaded yet.")}};
}
