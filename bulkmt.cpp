#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <time.h>
#include <chrono>
#include <ctime>
#include <iostream>
#include <fstream>
#include <thread>
#include <condition_variable>
#include <future>

int gRowCount = 1;

const std::string title_block = "block(s)";
const std::string title_command = "command(s)";
const std::string title_line = "line(s)";

std::mutex gConsoleMtx;

void ConsoleLog( const std::string& log_string )
{
	gConsoleMtx.lock();
	std::cout << log_string << std::endl;
	gConsoleMtx.unlock();
}

class Observer
{
public:
	virtual void execute( std::vector<std::string>*, time_t* ) = 0;
	virtual ~Observer() = default;
};

class Executor
{
public:
	Executor(): m_fct(time(0))
		, m_nReadySubscribersCount(0)
		,m_bReadyProcessData(true){};

	std::vector<std::string> m_commands;
	time_t m_fct; // first command time
	std::condition_variable m_cvReadyProcess;
	bool m_bReadyProcessData;
private:
	std::vector<std::shared_ptr<Observer>> m_subscribers;
	std::atomic_int m_nReadySubscribersCount;
	std::mutex m_Mutex;

public:
	void subscribe( std::shared_ptr<Observer> ptrObs )
	{
		m_subscribers.push_back( ptrObs );
		++m_nReadySubscribersCount;
	}

	void set_commands( std::vector<std::string> commands, time_t fct )
	{
		m_bReadyProcessData = false;
		m_commands = commands;
		m_fct = fct;

		execute();
	}

	void execute()
	{
		for( auto s : m_subscribers )
		{
			s->execute( &m_commands, &m_fct );
		}
	}

	void finish_task()
	{
		if( m_nReadySubscribersCount > 0 )
		{
			--m_nReadySubscribersCount;
			if( m_nReadySubscribersCount == 0 )
			{
				std::unique_lock<std::mutex> lck( m_Mutex );
				m_bReadyProcessData = true;
				m_cvReadyProcess.notify_all();

				m_nReadySubscribersCount = m_subscribers.size();
			}
		}
	}
};

class FileObserver: public Observer
{
public:
	FileObserver( std::shared_ptr<Executor> ptrExecutor ): m_bRun( true )
		, m_bDataExist( false)
		, m_pExecutor( ptrExecutor )
	{
		auto wptr = std::shared_ptr<FileObserver>( this, []( FileObserver* ) {} );
		ptrExecutor->subscribe( wptr );

		m_Thread1 = std::thread( &FileObserver::Run, this, 1 );
		m_Thread2 = std::thread( &FileObserver::Run, this, 2 );
	}

	void execute( std::vector<std::string>* commands, time_t* fct ) override
	{
		m_pCommandsVect = commands;
		m_pFirstCommandTime = fct;

		std::unique_lock<std::mutex> lck( m_Mutex );
		m_bDataExist = true;
		m_cv.notify_all();
	}

	~FileObserver()
	{
		m_bRun = false;
		m_bDataExist = true;
		m_cv.notify_all();

		if( m_Thread1.joinable() )
			m_Thread1.join();
		if( m_Thread2.joinable() )
			m_Thread2.join();

	}

private:
	bool m_bRun;
	std::thread m_Thread1;
	std::thread m_Thread2;
	std::mutex m_Mutex;
	bool m_bDataExist;
	std::condition_variable m_cv;

	std::vector<std::string>* m_pCommandsVect;
	time_t* m_pFirstCommandTime;

	std::shared_ptr<Executor> m_pExecutor;

	void Run( int thread )
	{
		while( m_bRun )
		{
			std::unique_lock<std::mutex> lck( m_Mutex );
			while( !m_bDataExist && m_bRun ) m_cv.wait( lck );
			if( !m_bRun ){ return; }

			int command_count = 0;
			int block_count = 0;

			if( m_pCommandsVect->size() > 0 )
			{
				struct tm  tstruct;
				char       buf[ 80 ];

				//tstruct = *localtime( m_pFirstCommandTime );

				localtime_r( &tstruct, m_pFirstCommandTime );
				strftime( buf, sizeof( buf ), "%OH%OM%OS", &tstruct );

				auto time_now = std::chrono::system_clock::now();
				auto ms = std::chrono::duration_cast< std::chrono::milliseconds >(time_now.time_since_epoch()) % 1000;

				std::string fn = "bulk_thread";
				fn.append( std::to_string( thread ) ).append("_");

				fn.append( buf );
				fn.append( std::to_string( ms.count() ) );
				fn.append( ".log" );

				std::ofstream myfile;
				myfile.open( fn );
				if( myfile.is_open() )
				{
					for( size_t i = 0; i < m_pCommandsVect->size(); ++i )
					{
						myfile << m_pCommandsVect->at( i );
						if( i != (m_pCommandsVect->size() - 1) )
							myfile << ", ";
						else
							myfile << std::endl;

						++command_count;
					}
					myfile.close();
					++block_count;

					std::string _out = "file";
					_out.append( std::to_string( thread ) ).append( " thread - " );
					_out.append( std::to_string( block_count ) ).append( " " ).append( title_block ).append( ", " );
					_out.append( std::to_string( command_count ) ).append( " " ).append( title_command );
					ConsoleLog( _out );

					// second thread does not wake up
					std::string _out2 = "file";
					_out2.append( std::to_string( (thread==1)?2:1 ) ).append( " thread - " );
					_out2.append( std::to_string( 0 ) ).append( " " ).append( title_block ).append( ", " );
					_out2.append( std::to_string( 0 ) ).append( " " ).append( title_command );
					ConsoleLog( _out2 );

					m_pExecutor->finish_task();
				}
			}
			m_bDataExist = false;
		}
	}
};

class ConsoleObserver: public Observer
{
public:
	ConsoleObserver( std::shared_ptr<Executor> ptrExecutor ):  m_bRun(true)
		, m_bDataExist(false)
		, m_pExecutor( ptrExecutor )
	{
		auto wptr = std::shared_ptr<ConsoleObserver>( this, []( ConsoleObserver* ) {} );
		ptrExecutor->subscribe( wptr );

		m_Thread = std::thread( &ConsoleObserver::Run, this );
	}

	void execute( std::vector<std::string>* commands, time_t* fct ) override
	{
		m_pCommandsVect = commands;
		m_pFirstCommandTime = fct;

		std::unique_lock<std::mutex> lck( m_Mutex );
		m_bDataExist = true;
		m_bRun = true;
		m_cv.notify_all();
	}

	~ConsoleObserver()
	{
		m_bRun = false;
		m_bDataExist = true;
		m_cv.notify_all();

		if( m_Thread.joinable() )
		{
			m_Thread.join();
		}
	}
private:
	bool m_bRun;
	std::thread m_Thread;
	std::mutex m_Mutex;
	bool m_bDataExist;
	std::condition_variable m_cv;


	std::vector<std::string>* m_pCommandsVect;
	time_t* m_pFirstCommandTime;

	std::shared_ptr<Executor> m_pExecutor;

	void Run()
	{
		while( m_bRun )
		{
			std::unique_lock<std::mutex> lck( m_Mutex );
			while( !m_bDataExist && m_bRun ) m_cv.wait( lck );

			if( !m_bRun )
			{
				return;
			}


			int command_count = 0;
			int block_count = 0;

			if( m_pCommandsVect->size() > 0 )
			{
				++block_count;
				
				std::string _out = "log thread - ";
				_out.append( std::to_string( block_count ) ).append( " " ).append( title_block ).append( ", " );
				_out.append( std::to_string( command_count ) ).append( " " ).append( title_command );
				ConsoleLog( _out );

				m_pExecutor->finish_task();
			}
			m_bDataExist = false;
		}
	}
};

class Parser
{
public:
	Parser( std::shared_ptr<Executor> ptrExec ): m_pExecutor( ptrExec ){};
	void Start()
	{
		if( m_pExecutor == nullptr )
		{
			std::cout << "ERROR Executor is null! Return";
			return;
		}

		int line_count = 0;
		int command_count = 0;
		int block_count = 0;

		int open_braces = 0;
		bool is_ready_data = false;

		std::vector<std::string> vector_str;
		int count = 1;
		time_t fct = time( 0 );
		std::string line;
		while( std::getline( std::cin, line ) )
		{
			++line_count;
			++command_count;
			if( count == 1 )
			{
				fct = time( 0 );
			}

			if( line.empty() ) // for exit sequence from console
			{
				is_ready_data = true;
				++block_count;
			}
			else if( line.find( '{' ) != std::string::npos )
			{
				++open_braces;
				--command_count;
				if( open_braces == 1 && count != 1 )
				{
					is_ready_data = true;
				}
			}
			else if( (line.find( '}' ) != std::string::npos) && (open_braces > 0) )
			{
				--open_braces;
				--command_count;
				if( open_braces == 0 )
				{
					is_ready_data = true;
				}
			}
			else if( (count == gRowCount) && (open_braces == 0) )
			{
				vector_str.push_back( line );
				is_ready_data = true;
			}
			else
			{
				vector_str.push_back( line );
			}

			if( is_ready_data )
			{
				std::string _result = "bulk: ";
				for( size_t i = 0; i < vector_str.size(); ++i )
				{
					++command_count;
					_result.append( vector_str.at( i ) );
					if( i < (vector_str.size() - 1) )
					{
						_result.append( ", " );
					}
				}
				ConsoleLog( _result );


				m_pExecutor->set_commands( vector_str, fct );
				vector_str.clear();
				++block_count;
				is_ready_data = false;

				std::string _out = "main thread - ";
				_out.append( std::to_string( line_count ) ).append( " " ).append( title_line ).append(", ");
				_out.append( std::to_string( command_count ) ).append( " " ).append( title_command ).append( ", " );
				_out.append( std::to_string( block_count ) ).append( " " ).append( title_block );

				ConsoleLog( _out );
				count = block_count = line_count = command_count = 0;

				std::unique_lock<std::mutex> lck( m_Mutex );
				while( !m_pExecutor->m_bReadyProcessData ) m_pExecutor->m_cvReadyProcess.wait( lck );

				int t = 0x00;
			}
			count++;
		}
		if( vector_str.size() > 0 )
		{
			m_pExecutor->set_commands( vector_str, fct );
			vector_str.clear();
			++block_count;

			std::string _out = "main thread - ";
			_out.append( std::to_string( line_count ) ).append( " " ).append( title_line ).append( ", " );
			_out.append( std::to_string( command_count ) ).append( " " ).append( title_command ).append( ", " );
			_out.append( std::to_string( block_count ) ).append( " " ).append( title_block );

			ConsoleLog( _out );
			count = block_count = line_count = command_count = 0;
		}
	}

private:
	std::shared_ptr<Executor> m_pExecutor;
	std::mutex m_Mutex;
	std::condition_variable m_cv;

};

int main( int argc, char *argv[] )
{
	if( argc >= 2 )
		gRowCount = std::stoi( argv[ 1 ] );

	std::shared_ptr<Executor> ptrExec( std::make_shared<Executor>( ) );

	FileObserver File( ptrExec );
	ConsoleObserver Console( ptrExec );

	Parser ParserWorker( ptrExec );
	ParserWorker.Start();

    return 0;
}