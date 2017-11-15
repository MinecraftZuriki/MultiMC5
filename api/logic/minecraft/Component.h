#pragma once

#include <memory>
#include <QList>
#include <QJsonDocument>
#include <QDateTime>
#include "ProblemProvider.h"
#include "multimc_logic_export.h"

class ComponentList;
class LaunchProfile;
namespace Meta
{
	class Version;
	class VersionList;
}
class VersionFile;

class MULTIMC_LOGIC_EXPORT Component : public ProblemProvider
{
public:
	Component(const QString &uid, const QString &filename);

	// DEPRECATED: remove these constructors?
	Component(std::shared_ptr<Meta::Version> version);
	Component(const QString & uid, std::shared_ptr<VersionFile> file, const QString &filename = QString());

	virtual ~Component(){};
	void applyTo(LaunchProfile *profile);

	bool isMoveable();
	bool isCustomizable();
	bool isRevertible();
	bool isRemovable();
	bool isCustom();
	bool isVersionChangeable();

	void setOrder(int order);
	int getOrder();

	QString getID();
	QString getName();
	QString getVersion();
	std::shared_ptr<Meta::Version> getMeta();
	QDateTime getReleaseDateTime();

	QString getFilename();

	std::shared_ptr<class VersionFile> getVersionFile() const;
	std::shared_ptr<class Meta::VersionList> getVersionList() const;

	void setVanilla (bool state);
	void setRemovable (bool state);
	void setRevertible (bool state);
	void setMovable (bool state);

	const QList<PatchProblem> getProblems() const override;
	ProblemSeverity getProblemSeverity() const override;

public: /* data */
	// Properties for UI and version manipulation from UI in general
	bool m_isMovable = false;
	bool m_isRevertible = false;
	bool m_isRemovable = false;
	bool m_isVanilla = false;

	// persistent component list properties
	QString m_uid;
	QString cachedName;
	QString m_currentVersion;

	// temporary component list properties (lost on full reload)
	bool m_orderOverride = false;
	int m_order = 0;

	// load state
	std::shared_ptr<Meta::Version> m_metaVersion;
	std::shared_ptr<VersionFile> m_file;
	QString m_filename;
	bool m_loaded = false;
};

typedef std::shared_ptr<Component> ComponentPtr;
