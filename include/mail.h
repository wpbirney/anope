#ifndef MAIL_H
#define MAIL_H

extern CoreExport bool Mail(User *u, NickRequest *nr, const std::string &service, const std::string &subject, const std::string &message);
extern CoreExport bool Mail(User *u, NickCore *nc, const std::string &service, const std::string &subject, const std::string &message);
extern CoreExport bool Mail(NickCore *nc, const std::string &subject, const std::string &message);
extern CoreExport bool MailValidate(const std::string &email);

class MailThread : public Thread
{
 private:
	std::string MailTo;
	std::string Addr;
	std::string Subject;
	std::string Message;

	bool Success;
 public:
	MailThread(const std::string &mailto, const std::string &addr, const std::string &subject, const std::string &message) : Thread(), MailTo(mailto), Addr(addr), Subject(subject), Message(message), Success(false)
	{
	}

	~MailThread();

	void Run();
};

#endif // MAIL_H
