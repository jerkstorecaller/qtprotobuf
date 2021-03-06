/*
 * MIT License
 *
 * Copyright (c) 2019 Alexey Edelev <semlanik@gmail.com>
 *
 * This file is part of QtProtobuf project https://git.semlanik.org/semlanik/qtprotobuf
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and
 * to permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "testservice_grpc.qpb.h"
#include <QGrpcHttp2Channel>
#include <QGrpcCredentials>
#include <QGrpcInsecureCredentials>

#include <QTimer>
#include <QFile>
#include <QCryptographicHash>
#include <QThread>

#include <QCoreApplication>
#include <gtest/gtest.h>

#include <qprotobufserializer.h>

using namespace qtprotobufnamespace::tests;
using namespace QtProtobuf;

class ClientTest : public ::testing::Test
{
protected:
    static void SetUpTestCase() {
        QtProtobuf::qRegisterProtobufTypes();
    }
    static QCoreApplication m_app;
    static int m_argc;
    static QUrl m_echoServerAddress;
};

int ClientTest::m_argc(0);
QCoreApplication ClientTest::m_app(m_argc, nullptr);
QUrl ClientTest::m_echoServerAddress("http://localhost:50051", QUrl::StrictMode);

TEST_F(ClientTest, CheckMethodsGeneration)
{
    //Dummy compile time check of functions generation and interface compatibility
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(QUrl(), QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleStringMessage request;
    QPointer<SimpleStringMessage> result(new SimpleStringMessage);
    testClient.testMethod(request, result);
    testClient.testMethod(request);
    testClient.testMethod(request, &testClient, [](QGrpcAsyncReplyShared) {});
    delete result;
}

TEST_F(ClientTest, StringEchoTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleStringMessage request;
    QPointer<SimpleStringMessage> result(new SimpleStringMessage);
    request.setTestFieldString("Hello beach!");
    ASSERT_TRUE(testClient.testMethod(request, result) == QGrpcStatus::Ok);
    ASSERT_STREQ(result->testFieldString().toStdString().c_str(), "Hello beach!");
    delete result;
}

TEST_F(ClientTest, StringEchoAsyncTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleStringMessage request;
    SimpleStringMessage result;
    request.setTestFieldString("Hello beach!");
    QEventLoop waiter;

    QGrpcAsyncReplyShared reply = testClient.testMethod(request);
    QObject::connect(reply.get(), &QGrpcAsyncReply::finished, &m_app, [reply, &result, &waiter]() {
        result = reply->read<SimpleStringMessage>();
        reply->deleteLater();
        waiter.quit();
    });

    waiter.exec();
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Hello beach!");
}

TEST_F(ClientTest, StringEchoAsync2Test)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Hello beach!");
    QEventLoop waiter;
    testClient.testMethod(request, &m_app, [&result, &waiter](QGrpcAsyncReplyShared reply) {
        result = reply->read<SimpleStringMessage>();
        waiter.quit();
    });

    waiter.exec();
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Hello beach!");
}

TEST_F(ClientTest, StringEchoImmediateAsyncAbortTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("sleep");
    QEventLoop waiter;
    QGrpcAsyncReplyShared reply = testClient.testMethod(request);

    result.setTestFieldString("Result not changed by echo");
    QObject::connect(reply.get(), &QGrpcAsyncReply::finished, &m_app, [&waiter, &result, reply]() {
        result = reply->read<SimpleStringMessage>();
        reply->deleteLater();
        waiter.quit();
    });

    QGrpcStatus::StatusCode asyncStatus = QGrpcStatus::StatusCode::Ok;
    QObject::connect(reply.get(), &QGrpcAsyncReply::error, [&asyncStatus](const QGrpcStatus &status) {
        asyncStatus = status.code();
    });

    QGrpcStatus::StatusCode clientStatus = QGrpcStatus::StatusCode::Ok;
    QObject::connect(&testClient, &TestServiceClient::error, [&clientStatus](const QGrpcStatus &status) {
        clientStatus = status.code();
        std::cerr << status.code() << ":" << status.message().toStdString();
    });

    QTimer::singleShot(5000, &waiter, &QEventLoop::quit);
    reply->abort();
    waiter.exec();

    ASSERT_EQ(clientStatus, QGrpcStatus::StatusCode::Aborted);
    ASSERT_EQ(asyncStatus, QGrpcStatus::StatusCode::Aborted);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Result not changed by echo");
}

TEST_F(ClientTest, StringEchoDeferredAsyncAbortTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("sleep");
    QEventLoop waiter;
    QGrpcAsyncReplyShared reply = testClient.testMethod(request);

    result.setTestFieldString("Result not changed by echo");
    bool errorCalled = false;
    reply = testClient.testMethod(request);
    QObject::connect(reply.get(), &QGrpcAsyncReply::finished, &m_app, [reply, &result, &waiter]() {
        result = reply->read<SimpleStringMessage>();
        waiter.quit();
    });
    QObject::connect(reply.get(), &QGrpcAsyncReply::error, [&errorCalled]() {
        errorCalled = true;
    });

    QTimer::singleShot(500, reply.get(), &QGrpcAsyncReply::abort);
    QTimer::singleShot(5000, &waiter, &QEventLoop::quit);

    waiter.exec();

    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Result not changed by echo");
    ASSERT_TRUE(errorCalled);
}

TEST_F(ClientTest, StringEchoStreamTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    QEventLoop waiter;

    int i = 0;
    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    QObject::connect(subscription.get(), &QGrpcSubscription::updated, &m_app, [&result, &i, &waiter, subscription]() {
        SimpleStringMessage ret = subscription->read<SimpleStringMessage>();

        ++i;

        result.setTestFieldString(result.testFieldString() + ret.testFieldString());

        if (i == 4) {
            waiter.quit();
        }
    });


    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(i, 4);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Stream1Stream2Stream3Stream4");
}

TEST_F(ClientTest, StringEchoStreamAbortTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    QEventLoop waiter;

    int i = 0;
    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    QObject::connect(subscription.get(), &QGrpcSubscription::updated, &m_app, [&result, &i, &waiter, subscription]() {
        SimpleStringMessage ret = subscription->read<SimpleStringMessage>();
        ++i;

        result.setTestFieldString(result.testFieldString() + ret.testFieldString());

        if (i == 3) {
            subscription->cancel();
            QTimer::singleShot(4000, &waiter, &QEventLoop::quit);
        }
    });

    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(i, 3);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Stream1Stream2Stream3");
}

TEST_F(ClientTest, StringEchoStreamAbortByTimerTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    QEventLoop waiter;


    int i = 0;
    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    QTimer::singleShot(3500, subscription.get(), [subscription]() {
        subscription->cancel();
    });

    bool isFinished = false;
    QObject::connect(subscription.get(), &QtProtobuf::QGrpcSubscription::finished, [&isFinished]() {
        isFinished = true;
    });

    bool isError = false;
    QObject::connect(subscription.get(), &QtProtobuf::QGrpcSubscription::error, [&isError]() {
        isError = true;
    });

    QObject::connect(subscription.get(), &QGrpcSubscription::updated, &m_app, [&result, &i, subscription]() {
        SimpleStringMessage ret = subscription->read<SimpleStringMessage>();
        ++i;

        result.setTestFieldString(result.testFieldString() + ret.testFieldString());
    });

    QTimer::singleShot(5000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(i, 3);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Stream1Stream2Stream3");
    ASSERT_TRUE(isFinished);
    ASSERT_TRUE(!isError);
}

TEST_F(ClientTest, StringEchoStreamTestRetUpdates)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request;
    QPointer<SimpleStringMessage> result(new SimpleStringMessage);

    request.setTestFieldString("Stream");

    QEventLoop waiter;

    testClient.subscribeTestMethodServerStreamUpdates(request, result);

    int i = 0;
    QObject::connect(result.data(), &SimpleStringMessage::testFieldStringChanged, &m_app, [&i]() {
        i++;
    });

    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(i, 4);
    ASSERT_STREQ(result->testFieldString().toStdString().c_str(), "Stream4");
    delete result;
}


TEST_F(ClientTest, HugeBlobEchoStreamTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    BlobMessage result;
    BlobMessage request;
    QFile testFile("testfile");
    ASSERT_TRUE(testFile.open(QFile::ReadOnly));

    request.setTestBytes(testFile.readAll());
    QByteArray dataHash = QCryptographicHash::hash(request.testBytes(), QCryptographicHash::Sha256);
    QEventLoop waiter;

    auto subscription = testClient.subscribeTestMethodBlobServerStreamUpdates(request);

    QObject::connect(subscription.get(), &QGrpcSubscription::updated, &m_app, [&result, &waiter, subscription]() {
        BlobMessage ret = subscription->read<BlobMessage>();
        result.setTestBytes(ret.testBytes());
        waiter.quit();
    });


    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    QByteArray returnDataHash = QCryptographicHash::hash(result.testBytes(), QCryptographicHash::Sha256);
    ASSERT_TRUE(returnDataHash == dataHash);
}

TEST_F(ClientTest, StatusMessageAsyncTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request(QString{"Some status message"});
    QGrpcStatus::StatusCode asyncStatus = QGrpcStatus::StatusCode::Ok;
    QEventLoop waiter;
    QString statusMessage;

    QGrpcAsyncReplyShared reply = testClient.testMethodStatusMessage(request);
    QObject::connect(reply.get(), &QGrpcAsyncReply::error, [&asyncStatus, &waiter, &statusMessage](const QGrpcStatus &status) {
        asyncStatus = status.code();
        statusMessage = status.message();
        waiter.quit();
    });

    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_STREQ(statusMessage.toStdString().c_str(), request.testFieldString().toStdString().c_str());
}

TEST_F(ClientTest, StatusMessageClientAsyncTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request(QString{"Some status message"});
    QGrpcStatus::StatusCode asyncStatus = QGrpcStatus::StatusCode::Ok;
    QEventLoop waiter;
    QString statusMessage;

    QObject::connect(&testClient, &TestServiceClient::error, [&asyncStatus, &waiter, &statusMessage](const QGrpcStatus &status) {
        asyncStatus = status.code();
        statusMessage = status.message();
        waiter.quit();
    });

    testClient.testMethodStatusMessage(request);

    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_STREQ(statusMessage.toStdString().c_str(), request.testFieldString().toStdString().c_str());
}

TEST_F(ClientTest, StatusMessageClientSyncTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request(QString{"Some status message"});
    QPointer<SimpleStringMessage> ret(new SimpleStringMessage);
    QGrpcStatus::StatusCode asyncStatus = QGrpcStatus::StatusCode::Ok;
    QEventLoop waiter;
    QString statusMessage;

    QObject::connect(&testClient, &TestServiceClient::error, [&asyncStatus, &waiter, &statusMessage](const QGrpcStatus &status) {
        asyncStatus = status.code();
        statusMessage = status.message();
        waiter.quit();
    });

    testClient.testMethodStatusMessage(request, ret);
    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_STREQ(statusMessage.toStdString().c_str(), request.testFieldString().toStdString().c_str());
    delete ret;
}

TEST_F(ClientTest, StatusMessageClientSyncTestReturnedStatus)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request(QString{"Some status message"});
    QPointer<SimpleStringMessage> ret(new SimpleStringMessage);
    QEventLoop waiter;
    QString statusMessage;

    QGrpcStatus status = testClient.testMethodStatusMessage(request, ret);

    ASSERT_STREQ(status.message().toStdString().c_str(), request.testFieldString().toStdString().c_str());
    delete ret;
}


TEST_F(ClientTest, ClientSyncTestUnattachedChannel)
{
    TestServiceClient testClient;
    SimpleStringMessage request(QString{"Some status message"});
    QPointer<SimpleStringMessage> ret(new SimpleStringMessage);
    QEventLoop waiter;

    QGrpcStatus status = testClient.testMethodStatusMessage(request, ret);

    ASSERT_EQ(status.code(), QGrpcStatus::Unknown);
    ASSERT_STREQ("No channel(s) attached.", status.message().toStdString().c_str());
    delete ret;
}

TEST_F(ClientTest, ClientSyncTestUnattachedChannelSignal)
{
    TestServiceClient testClient;
    SimpleStringMessage request(QString{"Some status message"});
    QPointer<SimpleStringMessage> ret(new SimpleStringMessage);
    QGrpcStatus asyncStatus(QGrpcStatus::StatusCode::Ok);
    QEventLoop waiter;

    QObject::connect(&testClient, &TestServiceClient::error, [&asyncStatus, &waiter](const QGrpcStatus &status) {
        asyncStatus = status;
        waiter.quit();
    });

    testClient.testMethodStatusMessage(request, ret);
    QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(asyncStatus, QGrpcStatus::Unknown);
    ASSERT_STREQ("No channel(s) attached.", asyncStatus.message().toStdString().c_str());
    delete ret;
}

TEST_F(ClientTest, AsyncReplySubscribeTest)
{
    QTimer callTimeout;
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage request(QString{"Some status message"});
    QGrpcStatus::StatusCode asyncStatus = QGrpcStatus::StatusCode::Ok;
    QEventLoop waiter;
    QString statusMessage;

    QObject::connect(&callTimeout, &QTimer::timeout, &waiter, &QEventLoop::quit);
    callTimeout.setInterval(5000);
    auto reply = testClient.testMethodStatusMessage(request);

    reply->subscribe(&m_app, []() {
        ASSERT_TRUE(false);
    },
    [&asyncStatus, &waiter, &statusMessage](const QGrpcStatus &status) {
        asyncStatus = status.code();
        statusMessage = status.message();
        waiter.quit();
    });

    callTimeout.start();
    waiter.exec();
    callTimeout.stop();

    ASSERT_STREQ(statusMessage.toStdString().c_str(), request.testFieldString().toStdString().c_str());

    SimpleStringMessage result;
    request.setTestFieldString("Hello beach!");

    reply = testClient.testMethod(request);
    reply->subscribe(&m_app, [reply, &result, &waiter]() {
        result = reply->read<SimpleStringMessage>();
        waiter.quit();
    });

    callTimeout.start();
    waiter.exec();
    callTimeout.stop();
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), request.testFieldString().toStdString().c_str());

    result.setTestFieldString("");
    request.setTestFieldString("Hello beach1!");

    reply = testClient.testMethod(request);
    reply->subscribe(&m_app, [reply, &result, &waiter]() {
        result = reply->read<SimpleStringMessage>();
        waiter.quit();
    }, []() {
        ASSERT_TRUE(false);
    });

    callTimeout.start();
    waiter.exec();
    callTimeout.stop();
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), request.testFieldString().toStdString().c_str());
}

TEST_F(ClientTest, MultipleSubscriptionsTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    QEventLoop waiter;
    request.setTestFieldString("Stream");

    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    auto subscriptionNext = testClient.subscribeTestMethodServerStreamUpdates(request);

    ASSERT_EQ(subscription, subscriptionNext);

    int i = 0;
    QObject::connect(subscription.get(), &QGrpcSubscription::updated, &m_app, [&result, &i, subscription]() {
        SimpleStringMessage ret = subscription->read<SimpleStringMessage>();
        ++i;

        result.setTestFieldString(result.testFieldString() + ret.testFieldString());
    });

    QTimer::singleShot(10000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_EQ(i, 4);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Stream1Stream2Stream3Stream4");
}

TEST_F(ClientTest, MultipleSubscriptionsCancelTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    auto subscriptionNext = testClient.subscribeTestMethodServerStreamUpdates(request);

    ASSERT_EQ(subscription, subscriptionNext);

    bool isFinished = false;
    QObject::connect(subscription.get(), &QtProtobuf::QGrpcSubscription::finished, [&isFinished]() {
        isFinished = true;
    });

    bool isFinishedNext = false;
    QObject::connect(subscriptionNext.get(), &QtProtobuf::QGrpcSubscription::finished, [&isFinishedNext]() {
        isFinishedNext = true;
    });

    subscriptionNext->cancel();

    ASSERT_TRUE(isFinished);
    ASSERT_TRUE(isFinishedNext);

    subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    ASSERT_NE(subscription, subscriptionNext);

    subscriptionNext = testClient.subscribeTestMethodServerStreamUpdates(request);

    ASSERT_EQ(subscription, subscriptionNext);

    isFinished = false;
    QObject::connect(subscription.get(), &QtProtobuf::QGrpcSubscription::finished, [&isFinished]() {
        isFinished = true;
    });

    isFinishedNext = false;
    QObject::connect(subscriptionNext.get(), &QtProtobuf::QGrpcSubscription::finished, [&isFinishedNext]() {
        isFinishedNext = true;
    });

    subscription->cancel();

    ASSERT_TRUE(isFinished);
    ASSERT_TRUE(isFinishedNext);
}

TEST_F(ClientTest, NonCompatibleArgRetTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleIntMessage request(2048);
    QPointer<SimpleStringMessage> result(new SimpleStringMessage);
    ASSERT_TRUE(testClient.testMethodNonCompatibleArgRet(request, result) == QGrpcStatus::Ok);
    ASSERT_STREQ(result->testFieldString().toStdString().c_str(), "2048");
    delete result;
}

TEST_F(ClientTest, StringEchoThreadTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleStringMessage request;
    QPointer<SimpleStringMessage> result(new SimpleStringMessage);
    request.setTestFieldString("Hello beach from thread!");
    bool ok = false;
    std::shared_ptr<QThread> thread(QThread::create([&](){
        ok = testClient.testMethod(request, result) == QGrpcStatus::Ok;
    }));

    thread->start();
    QEventLoop wait;
    QTimer::singleShot(2000, &wait, &QEventLoop::quit);
    wait.exec();

    ASSERT_TRUE(ok);
    ASSERT_STREQ(result->testFieldString().toStdString().c_str(), "Hello beach from thread!");
    delete result;

    //Delete result pointer in between call operations
    result = new SimpleStringMessage();
    ok = false;
    thread.reset(QThread::create([&](){
        ok = testClient.testMethod(request, result) == QGrpcStatus::Ok;
    }));

    thread->start();
    delete result;
    QTimer::singleShot(2000, &wait, &QEventLoop::quit);
    wait.exec();

    ASSERT_TRUE(!ok);
}


TEST_F(ClientTest, StringEchoAsyncThreadTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureChannelCredentials() | QGrpcInsecureCallCredentials()));
    SimpleStringMessage request;
    SimpleStringMessage result;
    request.setTestFieldString("Hello beach from thread!");

    bool threadsOk = true;
    bool replyDestroyed = true;
    std::shared_ptr<QThread> thread(QThread::create([&](){
        QEventLoop waiter;
        QThread *validThread = QThread::currentThread();
        QGrpcAsyncReplyShared reply = testClient.testMethod(request);
        QObject::connect(reply.get(), &QObject::destroyed, [&replyDestroyed]{replyDestroyed = true;});
        QObject::connect(reply.get(), &QGrpcAsyncReply::finished, &waiter, [reply, &result, &waiter, &threadsOk, validThread]() {
            threadsOk &= reply->thread() != QThread::currentThread();
            threadsOk &= validThread == QThread::currentThread();
            result = reply->read<SimpleStringMessage>();
            waiter.quit();
        });
        threadsOk &= reply->thread() != QThread::currentThread();
        waiter.exec();
    }));

    thread->start();
    QEventLoop wait;
    QTimer::singleShot(2000, &wait, &QEventLoop::quit);
    wait.exec();
    ASSERT_TRUE(replyDestroyed);
    ASSERT_TRUE(threadsOk);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Hello beach from thread!");
}

TEST_F(ClientTest, StringEchoStreamThreadTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    int i = 0;
    bool threadsOk = true;
    std::shared_ptr<QThread> thread(QThread::create([&](){
        QEventLoop waiter;
        QThread *validThread = QThread::currentThread();
        auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
        QObject::connect(subscription.get(), &QGrpcSubscription::updated, &waiter, [&result, &i, &waiter, subscription, &threadsOk, validThread]() {
            SimpleStringMessage ret = subscription->read<SimpleStringMessage>();
            result.setTestFieldString(result.testFieldString() + ret.testFieldString());
            ++i;
            if (i == 4) {
                waiter.quit();
            }
            threadsOk &= subscription->thread() != QThread::currentThread();
            threadsOk &= validThread == QThread::currentThread();
        });

        threadsOk &= subscription->thread() != QThread::currentThread();
        QTimer::singleShot(20000, &waiter, &QEventLoop::quit);
        waiter.exec();
    }));

    thread->start();
    QEventLoop wait;
    QObject::connect(thread.get(), &QThread::finished, &wait, [&wait]{ wait.quit(); });
    QTimer::singleShot(20000, &wait, &QEventLoop::quit);
    wait.exec();

    ASSERT_TRUE(threadsOk);
    ASSERT_EQ(i, 4);
    ASSERT_STREQ(result.testFieldString().toStdString().c_str(), "Stream1Stream2Stream3Stream4");
}

TEST_F(ClientTest, AttachChannelThreadTest)
{
    std::shared_ptr<QGrpcHttp2Channel> channel;
    std::shared_ptr<QThread> thread(QThread::create([&](){
        channel = std::make_shared<QGrpcHttp2Channel>(m_echoServerAddress, QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials());
    }));
    thread->start();
    QThread::msleep(1000);
    TestServiceClient testClient;
    EXPECT_DEATH({
                     testClient.attachChannel(channel);
                 }, ".*");
}

TEST_F(ClientTest, StreamCancelWhileErrorTimeoutTest)
{
    TestServiceClient testClient;
    testClient.attachChannel(std::make_shared<QGrpcHttp2Channel>(QUrl("http://localhost:50052", QUrl::StrictMode), QGrpcInsecureCallCredentials() | QGrpcInsecureChannelCredentials()));//Invalid port
    SimpleStringMessage result;
    SimpleStringMessage request;
    request.setTestFieldString("Stream");

    QEventLoop waiter;

    bool ok = false;
    auto subscription = testClient.subscribeTestMethodServerStreamUpdates(request);
    QObject::connect(subscription.get(), &QGrpcSubscription::finished, &m_app, [&ok, &waiter]() {
        ok = true;
        waiter.quit();
    });
    subscription->cancel();
    subscription.reset();

    QTimer::singleShot(5000, &waiter, &QEventLoop::quit);
    waiter.exec();

    ASSERT_TRUE(ok);
}

