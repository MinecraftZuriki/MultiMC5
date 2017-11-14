/* Copyright 2013-2017 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <QFile>
#include <QCryptographicHash>
#include <Version.h>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

#include "minecraft/ComponentList.h"
#include "Exception.h"
#include <minecraft/OneSixVersionFormat.h>
#include <FileSystem.h>
#include <QSaveFile>
#include <Env.h>
#include <meta/Index.h>
#include <minecraft/MinecraftInstance.h>
#include <QUuid>
/*
struct Component
{
	QString uid;
	QString cachedName;
	QString currentVersion;
	bool asDependency = false;
	bool pinned = false;
	// does this use a file?
	bool usesFile = false;
	// caching mechanism: do not reload file unless its timestamp has changed
	QDateTime fileTimestamp;
};
using ComponentPtr = std::unique_ptr<Component>;
*/
struct ComponentListData
{
	/// list of attached profile patches
	QList<ComponentPtr> m_patches;

	// the instance this belongs to
	MinecraftInstance *m_instance;

	std::shared_ptr<LaunchProfile> m_profile;

	// NOTE: This is here for conversion from instance.cfg only
	std::map<QString, QString> m_oldConfigVersions;

	// persistent list of components
	std::vector<ComponentPtr> components;
	std::map<QString, size_t> componentIndex;
};

ComponentList::ComponentList(MinecraftInstance * instance)
	: QAbstractListModel()
{
	d.reset(new ComponentListData);
	d->m_instance = instance;
}

ComponentList::~ComponentList()
{
}

void ComponentList::reload()
{
	beginResetModel();
	load();
	reapplyPatches();
	endResetModel();
}

void ComponentList::appendPatch(ComponentPtr patch)
{
	int index = d->m_patches.size();
	beginInsertRows(QModelIndex(), index, index);
	d->m_patches.append(patch);
	endInsertRows();
}

bool ComponentList::remove(const int index)
{
	auto patch = getComponent(index);
	if (!patch->isRemovable())
	{
		qDebug() << "Patch" << patch->getID() << "is non-removable";
		return false;
	}

	if(!removeComponent_internal(patch))
	{
		qCritical() << "Patch" << patch->getID() << "could not be removed";
		return false;
	}

	beginRemoveRows(QModelIndex(), index, index);
	d->m_patches.removeAt(index);
	endRemoveRows();
	reapplyPatches();
	saveCurrentOrder();
	return true;
}

bool ComponentList::remove(const QString id)
{
	int i = 0;
	for (auto patch : d->m_patches)
	{
		if (patch->getID() == id)
		{
			return remove(i);
		}
		i++;
	}
	return false;
}

bool ComponentList::customize(int index)
{
	auto patch = getComponent(index);
	if (!patch->isCustomizable())
	{
		qDebug() << "Patch" << patch->getID() << "is not customizable";
		return false;
	}
	if(!customizeComponent_internal(patch))
	{
		qCritical() << "Patch" << patch->getID() << "could not be customized";
		return false;
	}
	reapplyPatches();
	saveCurrentOrder();
	// FIXME: maybe later in unstable
	// emit dataChanged(createIndex(index, 0), createIndex(index, columnCount(QModelIndex()) - 1));
	return true;
}

bool ComponentList::revertToBase(int index)
{
	auto patch = getComponent(index);
	if (!patch->isRevertible())
	{
		qDebug() << "Patch" << patch->getID() << "is not revertible";
		return false;
	}
	if(!revertComponent_internal(patch))
	{
		qCritical() << "Patch" << patch->getID() << "could not be reverted";
		return false;
	}
	reapplyPatches();
	saveCurrentOrder();
	// FIXME: maybe later in unstable
	// emit dataChanged(createIndex(index, 0), createIndex(index, columnCount(QModelIndex()) - 1));
	return true;
}

ComponentPtr ComponentList::getComponent(const QString &id)
{
	for (auto patch : d->m_patches)
	{
		if (patch->getID() == id)
		{
			return patch;
		}
	}
	return nullptr;
}

ComponentPtr ComponentList::getComponent(int index)
{
	if(index < 0 || index >= d->m_patches.size())
		return nullptr;
	return d->m_patches[index];
}

bool ComponentList::isVanilla()
{
	for(auto patchptr: d->m_patches)
	{
		if(patchptr->isCustom())
			return false;
	}
	return true;
}

bool ComponentList::revertToVanilla()
{
	// remove patches, if present
	auto VersionPatchesCopy = d->m_patches;
	for(auto & it: VersionPatchesCopy)
	{
		if (!it->isCustom())
		{
			continue;
		}
		if(it->isRevertible() || it->isRemovable())
		{
			if(!remove(it->getID()))
			{
				qWarning() << "Couldn't remove" << it->getID() << "from profile!";
				reapplyPatches();
				saveCurrentOrder();
				return false;
			}
		}
	}
	reapplyPatches();
	saveCurrentOrder();
	return true;
}

QVariant ComponentList::data(const QModelIndex &index, int role) const
{
	if (!index.isValid())
		return QVariant();

	int row = index.row();
	int column = index.column();

	if (row < 0 || row >= d->m_patches.size())
		return QVariant();

	auto patch = d->m_patches.at(row);

	if (role == Qt::DisplayRole)
	{
		switch (column)
		{
		case 0:
			return d->m_patches.at(row)->getName();
		case 1:
		{
			if(patch->isCustom())
			{
				return QString("%1 (Custom)").arg(patch->getVersion());
			}
			else
			{
				return patch->getVersion();
			}
		}
		default:
			return QVariant();
		}
	}
	if(role == Qt::DecorationRole)
	{
		switch(column)
		{
		case 0:
		{
			auto severity = patch->getProblemSeverity();
			switch (severity)
			{
				case ProblemSeverity::Warning:
					return "warning";
				case ProblemSeverity::Error:
					return "error";
				default:
					return QVariant();
			}
		}
		default:
		{
			return QVariant();
		}
		}
	}
	return QVariant();
}
QVariant ComponentList::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation == Qt::Horizontal)
	{
		if (role == Qt::DisplayRole)
		{
			switch (section)
			{
			case 0:
				return tr("Name");
			case 1:
				return tr("Version");
			default:
				return QVariant();
			}
		}
	}
	return QVariant();
}
Qt::ItemFlags ComponentList::flags(const QModelIndex &index) const
{
	if (!index.isValid())
		return Qt::NoItemFlags;
	return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

int ComponentList::rowCount(const QModelIndex &parent) const
{
	return d->m_patches.size();
}

int ComponentList::columnCount(const QModelIndex &parent) const
{
	return 2;
}

void ComponentList::saveCurrentOrder() const
{
	ProfileUtils::PatchOrder order;
	for(auto item: d->m_patches)
	{
		if(!item->isMoveable())
			continue;
		order.append(item->getID());
	}
	saveOrder_internal(order);
}

void ComponentList::move(const int index, const MoveDirection direction)
{
	int theirIndex;
	if (direction == MoveUp)
	{
		theirIndex = index - 1;
	}
	else
	{
		theirIndex = index + 1;
	}

	if (index < 0 || index >= d->m_patches.size())
		return;
	if (theirIndex >= rowCount())
		theirIndex = rowCount() - 1;
	if (theirIndex == -1)
		theirIndex = rowCount() - 1;
	if (index == theirIndex)
		return;
	int togap = theirIndex > index ? theirIndex + 1 : theirIndex;

	auto from = getComponent(index);
	auto to = getComponent(theirIndex);

	if (!from || !to || !to->isMoveable() || !from->isMoveable())
	{
		return;
	}
	beginMoveRows(QModelIndex(), index, index, QModelIndex(), togap);
	d->m_patches.swap(index, theirIndex);
	endMoveRows();
	reapplyPatches();
	saveCurrentOrder();
}
void ComponentList::resetOrder()
{
	resetOrder_internal();
	reload();
}

// FIXME: this should either erase the current launch profile or mark it as dirty in some way
bool ComponentList::reapplyPatches()
{
	try
	{
		d->m_profile.reset(new LaunchProfile);
		for(auto file: d->m_patches)
		{
			qDebug() << "Applying" << file->getID() << (file->getProblemSeverity() == ProblemSeverity::Error ? "ERROR" : "GOOD");
			file->applyTo(d->m_profile.get());
		}
	}
	catch (Exception & error)
	{
		d->m_profile.reset();
		qWarning() << "Couldn't apply profile patches because: " << error.cause();
		return false;
	}
	return true;
}

void ComponentList::installJarMods(QStringList selectedFiles)
{
	installJarMods_internal(selectedFiles);
}

void ComponentList::installCustomJar(QString selectedFile)
{
	installCustomJar_internal(selectedFile);
}


/*
 * TODO: get rid of this. Get rid of all order numbers.
 */
int ComponentList::getFreeOrderNumber()
{
	int largest = 100;
	// yes, I do realize this is dumb. The order thing itself is dumb. and to be removed next.
	for(auto thing: d->m_patches)
	{
		int order = thing->getOrder();
		if(order > largest)
			largest = order;
	}
	return largest + 1;
}

// NOTE this is really old stuff, and only needs to be used when loading the old hardcoded component-unaware format (loadPreComponentConfig).
static void upgradeDeprecatedFiles(QString root, QString instanceName)
{
	auto versionJsonPath = FS::PathCombine(root, "version.json");
	auto customJsonPath = FS::PathCombine(root, "custom.json");
	auto mcJson = FS::PathCombine(root, "patches" , "net.minecraft.json");

	QString sourceFile;
	QString renameFile;

	// convert old crap.
	if(QFile::exists(customJsonPath))
	{
		sourceFile = customJsonPath;
		renameFile = versionJsonPath;
	}
	else if(QFile::exists(versionJsonPath))
	{
		sourceFile = versionJsonPath;
	}
	if(!sourceFile.isEmpty() && !QFile::exists(mcJson))
	{
		if(!FS::ensureFilePathExists(mcJson))
		{
			qWarning() << "Couldn't create patches folder for" << instanceName;
			return;
		}
		if(!renameFile.isEmpty() && QFile::exists(renameFile))
		{
			if(!QFile::rename(renameFile, renameFile + ".old"))
			{
				qWarning() << "Couldn't rename" << renameFile << "to" << renameFile + ".old" << "in" << instanceName;
				return;
			}
		}
		auto file = ProfileUtils::parseJsonFile(QFileInfo(sourceFile), false);
		ProfileUtils::removeLwjglFromPatch(file);
		file->uid = "net.minecraft";
		file->version = file->minecraftVersion;
		file->name = "Minecraft";
		file->requires.insert(Meta::Require("org.lwjgl"));
		auto data = OneSixVersionFormat::versionFileToJson(file, false).toJson();
		QSaveFile newPatchFile(mcJson);
		if(!newPatchFile.open(QIODevice::WriteOnly))
		{
			newPatchFile.cancelWriting();
			qWarning() << "Couldn't open main patch for writing in" << instanceName;
			return;
		}
		newPatchFile.write(data);
		if(!newPatchFile.commit())
		{
			qWarning() << "Couldn't save main patch in" << instanceName;
			return;
		}
		if(!QFile::rename(sourceFile, sourceFile + ".old"))
		{
			qWarning() << "Couldn't rename" << sourceFile << "to" << sourceFile + ".old" << "in" << instanceName;
			return;
		}
	}
}

void ComponentList::loadPreComponentConfig()
{
	upgradeDeprecatedFiles(d->m_instance->instanceRoot(), d->m_instance->name());
	auto addBuiltinPatch = [&](const QString &uid, const QString intendedVersion, int order)
	{
		auto jsonFilePath = FS::PathCombine(d->m_instance->instanceRoot(), "patches" , uid + ".json");
		// load up the base minecraft patch
		ComponentPtr profilePatch;
		if(QFile::exists(jsonFilePath))
		{
			auto file = ProfileUtils::parseJsonFile(QFileInfo(jsonFilePath), false);
			if(file->version.isEmpty())
			{
				file->version = intendedVersion;
			}
			profilePatch = std::make_shared<Component>(file, jsonFilePath);
			profilePatch->setVanilla(false);
			profilePatch->setRevertible(true);
		}
		else
		{
			auto metaVersion = ENV.metadataIndex()->get(uid, intendedVersion);
			profilePatch = std::make_shared<Component>(metaVersion);
			profilePatch->setVanilla(true);
		}
		profilePatch->setOrder(order);
		appendPatch(profilePatch);
	};
	addBuiltinPatch("net.minecraft", getComponentVersion("net.minecraft"), -2);
	addBuiltinPatch("org.lwjgl", getComponentVersion("org.lwjgl"), -1);

	// first, collect all patches (that are not builtins of OneSix) and load them
	QMap<QString, ComponentPtr> loadedPatches;
	QDir patchesDir(FS::PathCombine(d->m_instance->instanceRoot(),"patches"));
	for (auto info : patchesDir.entryInfoList(QStringList() << "*.json", QDir::Files))
	{
		// parse the file
		qDebug() << "Reading" << info.fileName();
		auto file = ProfileUtils::parseJsonFile(info, true);
		// ignore builtins
		if (file->uid == "net.minecraft")
			continue;
		if (file->uid == "org.lwjgl")
			continue;
		auto patch = std::make_shared<Component>(file, info.filePath());
		patch->setRemovable(true);
		patch->setMovable(true);
		if(ENV.metadataIndex()->hasUid(file->uid))
		{
			// FIXME: requesting a uid/list creates it in the index... this allows reverting to possibly invalid versions...
			patch->setRevertible(true);
		}
		loadedPatches[file->uid] = patch;
	}
	// these are 'special'... if not already loaded from instance files, grab them from the metadata repo.
	auto loadSpecial = [&](const QString & uid, int order)
	{
		auto patchVersion = getComponentVersion(uid);
		if(!patchVersion.isEmpty() && !loadedPatches.contains(uid))
		{
			auto patch = std::make_shared<Component>(ENV.metadataIndex()->get(uid, patchVersion));
			patch->setOrder(order);
			patch->setVanilla(true);
			patch->setRemovable(true);
			patch->setMovable(true);
			loadedPatches[uid] = patch;
		}
	};
	loadSpecial("net.minecraftforge", 5);
	loadSpecial("com.mumfrey.liteloader", 10);

	// now add all the patches by user sort order
	ProfileUtils::PatchOrder userOrder;
	ProfileUtils::readOverrideOrders(FS::PathCombine(d->m_instance->instanceRoot(), "order.json"), userOrder);
	for (auto uid : userOrder)
	{
		// ignore builtins
		if (uid == "net.minecraft")
			continue;
		if (uid == "org.lwjgl")
			continue;
		// ordering has a patch that is gone?
		if(!loadedPatches.contains(uid))
		{
			continue;
		}
		appendPatch(loadedPatches.take(uid));
	}

	// is there anything left to sort?
	if(loadedPatches.isEmpty())
	{
		// TODO: save the order here?
		return;
	}

	// inserting into multimap by order number as key sorts the patches and detects duplicates
	QMultiMap<int, ComponentPtr> files;
	auto iter = loadedPatches.begin();
	while(iter != loadedPatches.end())
	{
		files.insert((*iter)->getOrder(), *iter);
		iter++;
	}

	// then just extract the patches and put them in the list
	for (auto order : files.keys())
	{
		const auto &values = files.values(order);
		for(auto &value: values)
		{
			// TODO: put back the insertion of problem messages here, so the user knows about the id duplication
			appendPatch(value);
		}
	}
	// TODO: save the order here?
}


void ComponentList::load()
{
	// FIXME: actually use fine-grained updates, not this...
	beginResetModel();
	d->m_patches.clear();
	endResetModel();
	QFile componentsFile(FS::PathCombine(d->m_instance->instanceRoot(), "mmc-pack.json"));
	if(componentsFile.exists())
	{
		// well, nothing yet...
	}
	else
	{
		loadPreComponentConfig();
	}
}

bool ComponentList::saveOrder_internal(ProfileUtils::PatchOrder order) const
{
	return ProfileUtils::writeOverrideOrders(FS::PathCombine(d->m_instance->instanceRoot(), "order.json"), order);
}

bool ComponentList::resetOrder_internal()
{
	return QDir(d->m_instance->instanceRoot()).remove("order.json");
}

bool ComponentList::removeComponent_internal(ComponentPtr patch)
{
	bool ok = true;
	// first, remove the patch file. this ensures it's not used anymore
	auto fileName = patch->getFilename();
	if(fileName.size())
	{
		QFile patchFile(fileName);
		if(patchFile.exists() && !patchFile.remove())
		{
			qCritical() << "File" << fileName << "could not be removed because:" << patchFile.errorString();
			return false;
		}
	}
	if(!getComponentVersion(patch->getID()).isEmpty())
	{
		setComponentVersion(patch->getID(), QString());
	}

	// FIXME: we need a generic way of removing local resources, not just jar mods...
	auto preRemoveJarMod = [&](LibraryPtr jarMod) -> bool
	{
		if (!jarMod->isLocal())
		{
			return true;
		}
		QStringList jar, temp1, temp2, temp3;
		jarMod->getApplicableFiles(currentSystem, jar, temp1, temp2, temp3, d->m_instance->jarmodsPath().absolutePath());
		QFileInfo finfo (jar[0]);
		if(finfo.exists())
		{
			QFile jarModFile(jar[0]);
			if(!jarModFile.remove())
			{
				qCritical() << "File" << jar[0] << "could not be removed because:" << jarModFile.errorString();
				return false;
			}
			return true;
		}
		return true;
	};

	auto &jarMods = patch->getVersionFile()->jarMods;
	for(auto &jarmod: jarMods)
	{
		ok &= preRemoveJarMod(jarmod);
	}
	return ok;
}

bool ComponentList::customizeComponent_internal(ComponentPtr patch)
{
	if(patch->isCustom())
	{
		return false;
	}

	auto filename = FS::PathCombine(d->m_instance->instanceRoot(), "patches" , patch->getID() + ".json");
	if(!FS::ensureFilePathExists(filename))
	{
		return false;
	}
	// FIXME: get rid of this try-catch.
	try
	{
		QSaveFile jsonFile(filename);
		if(!jsonFile.open(QIODevice::WriteOnly))
		{
			return false;
		}
		auto vfile = patch->getVersionFile();
		if(!vfile)
		{
			return false;
		}
		auto document = OneSixVersionFormat::versionFileToJson(vfile, true);
		jsonFile.write(document.toJson());
		if(!jsonFile.commit())
		{
			return false;
		}
		load();
	}
	catch (Exception &error)
	{
		qWarning() << "Version could not be loaded:" << error.cause();
	}
	return true;
}

bool ComponentList::revertComponent_internal(ComponentPtr patch)
{
	if(!patch->isCustom())
	{
		// already not custom
		return true;
	}
	auto filename = patch->getFilename();
	if(!QFile::exists(filename))
	{
		// already gone / not custom
		return true;
	}
	// just kill the file and reload
	bool result = QFile::remove(filename);
	// FIXME: get rid of this try-catch.
	try
	{
		load();
	}
	catch (Exception &error)
	{
		qWarning() << "Version could not be loaded:" << error.cause();
	}
	return result;
}

bool ComponentList::installJarMods_internal(QStringList filepaths)
{
	QString patchDir = FS::PathCombine(d->m_instance->instanceRoot(), "patches");
	if(!FS::ensureFolderPathExists(patchDir))
	{
		return false;
	}

	if (!FS::ensureFolderPathExists(d->m_instance->jarModsDir()))
	{
		return false;
	}

	for(auto filepath:filepaths)
	{
		QFileInfo sourceInfo(filepath);
		auto uuid = QUuid::createUuid();
		QString id = uuid.toString().remove('{').remove('}');
		QString target_filename = id + ".jar";
		QString target_id = "org.multimc.jarmod." + id;
		QString target_name = sourceInfo.completeBaseName() + " (jar mod)";
		QString finalPath = FS::PathCombine(d->m_instance->jarModsDir(), target_filename);

		QFileInfo targetInfo(finalPath);
		if(targetInfo.exists())
		{
			return false;
		}

		if (!QFile::copy(sourceInfo.absoluteFilePath(),QFileInfo(finalPath).absoluteFilePath()))
		{
			return false;
		}

		auto f = std::make_shared<VersionFile>();
		auto jarMod = std::make_shared<Library>();
		jarMod->setRawName(GradleSpecifier("org.multimc.jarmods:" + id + ":1"));
		jarMod->setFilename(target_filename);
		jarMod->setDisplayName(sourceInfo.completeBaseName());
		jarMod->setHint("local");
		f->jarMods.append(jarMod);
		f->name = target_name;
		f->uid = target_id;
		f->order = getFreeOrderNumber();
		QString patchFileName = FS::PathCombine(patchDir, target_id + ".json");

		QFile file(patchFileName);
		if (!file.open(QFile::WriteOnly))
		{
			qCritical() << "Error opening" << file.fileName()
						<< "for reading:" << file.errorString();
			return false;
		}
		file.write(OneSixVersionFormat::versionFileToJson(f, true).toJson());
		file.close();

		auto patch = std::make_shared<Component>(f, patchFileName);
		patch->setMovable(true);
		patch->setRemovable(true);
		appendPatch(patch);
	}
	saveCurrentOrder();
	reapplyPatches();
	return true;
}

bool ComponentList::installCustomJar_internal(QString filepath)
{
	QString patchDir = FS::PathCombine(d->m_instance->instanceRoot(), "patches");
	if(!FS::ensureFolderPathExists(patchDir))
	{
		return false;
	}

	QString libDir = d->m_instance->getLocalLibraryPath();
	if (!FS::ensureFolderPathExists(libDir))
	{
		return false;
	}

	auto specifier = GradleSpecifier("org.multimc:customjar:1");
	QFileInfo sourceInfo(filepath);
	QString target_filename = specifier.getFileName();
	QString target_id = specifier.artifactId();
	QString target_name = sourceInfo.completeBaseName() + " (custom jar)";
	QString finalPath = FS::PathCombine(libDir, target_filename);

	QFileInfo jarInfo(finalPath);
	if (jarInfo.exists())
	{
		if(!QFile::remove(finalPath))
		{
			return false;
		}
	}
	if (!QFile::copy(filepath, finalPath))
	{
		return false;
	}

	auto f = std::make_shared<VersionFile>();
	auto jarMod = std::make_shared<Library>();
	jarMod->setRawName(specifier);
	jarMod->setDisplayName(sourceInfo.completeBaseName());
	jarMod->setHint("local");
	f->mainJar = jarMod;
	f->name = target_name;
	f->uid = target_id;
	f->order = getFreeOrderNumber();
	QString patchFileName = FS::PathCombine(patchDir, target_id + ".json");

	QFile file(patchFileName);
	if (!file.open(QFile::WriteOnly))
	{
		qCritical() << "Error opening" << file.fileName()
					<< "for reading:" << file.errorString();
		return false;
	}
	file.write(OneSixVersionFormat::versionFileToJson(f, true).toJson());
	file.close();

	auto patch = std::make_shared<Component>(f, patchFileName);
	patch->setMovable(true);
	patch->setRemovable(true);
	appendPatch(patch);

	saveCurrentOrder();
	reapplyPatches();
	return true;
}

std::shared_ptr<LaunchProfile> ComponentList::getProfile() const
{
	return d->m_profile;
}

void ComponentList::setOldConfigVersion(const QString& uid, const QString& version)
{
	if(version.isEmpty())
	{
		return;
	}
	d->m_oldConfigVersions[uid] = version;
}

bool ComponentList::setComponentVersion(const QString& uid, const QString& version)
{
	return false;
}

QString ComponentList::getComponentVersion(const QString& uid) const
{
	const auto iter = d->m_oldConfigVersions.find(uid);
	if (iter != d->m_oldConfigVersions.end())
	{
		return (*iter).second;
	}
	return QString();
}
